#include "ui/extensions/NativeViewWidget.h"
#include "ui/Theme.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/ChoiceSlider.h"
#include "ui/components/CircularProgress.h"
#include "ui/components/Controls.h"
#include "ui/components/ElidingButton.h"
#include "ui/components/ElidingLabel.h"
#include "ui/components/IconLabelButton.h"
#include "ui/components/ShimmerLabel.h"
#include "ui/extensions/NativeContentRow.h"
#include "ui/extensions/NativeDialog.h"
#include "ui/extensions/NativeHoverGroup.h"
#include "ui/extensions/NativeImageView.h"
#include "ui/extensions/NativeListDelegate.h"
#include "ui/extensions/NativeMarkdownView.h"
#include "ui/extensions/NativeMenu.h"
#include "ui/extensions/NativeTextEdit.h"
#include "ui/extensions/NativeTranscriptView.h"
#include "ui/extensions/NativeViewScrollState.h"
#include "ui/extensions/NativeViewTextState.h"
#include "ui/styles/StyleTemplate.h"
#include <QAction>
#include <QApplication>
#include <QBoxLayout>
#include <QFontDatabase>
#include <QJsonArray>
#include <QJsonDocument>
#include <QKeyEvent>
#include <QLabel>
#include <QMenu>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QScopeGuard>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalBlocker>
#include <QStyle>
#include <QTextBrowser>
#include <QTextDocument>
#include <QTimer>
#include <QUuid>
#include <algorithm>
namespace litecode::ui {
using namespace litecode::extensions;
namespace {
void retire(QWidget* widget, QObject* receiver) {
    widget->disconnect(receiver);
    // QMenu is a top-level popup even when its QObject parent is a button.
    // Hiding/reparenting that button does not release the popup's input grab.
    // Close nested popups first, before deferred widget deletion can be delayed
    // by another event loop or by a continuing stream of native view updates.
    QVector<QPointer<QMenu>> menus;
    for (auto* menu : widget->findChildren<QMenu*>())
        menus.prepend(menu);
    for (const auto& menu : menus)
        if (menu)
            menu->close();
    for (auto* dialog : widget->findChildren<QDialog*>())
        dialog->hide();
    for (auto* child : widget->findChildren<QObject*>())
        child->disconnect(receiver);
    widget->hide();
    widget->setParent(nullptr);
    widget->deleteLater();
}
void synchronizeBox(QBoxLayout* layout, const QVector<QPair<QWidget*, int>>& children) {
    QSet<QWidget*> desired;
    for (const auto& child : children)
        desired.insert(child.first);
    for (int index = layout->count() - 1; index >= 0; --index)
        if (!desired.contains(layout->itemAt(index)->widget()))
            delete layout->takeAt(index);
    for (int index = 0; index < children.size(); ++index) {
        const auto& child = children[index];
        if (layout->indexOf(child.first) != index) {
            layout->removeWidget(child.first);
            layout->insertWidget(index, child.first, child.second);
        } else
            layout->setStretch(index, child.second);
    }
}
class CardLabel final : public QLabel {
  public:
    using QLabel::QLabel;
    QSize sizeHint() const override {
        auto size = QLabel::sizeHint();
        int width = 0;
        for (const auto& line : text().left(4096).split('\n'))
            width = qMax(width, fontMetrics().horizontalAdvance(line));
        size.setWidth(width);
        return size;
    }
    QSize minimumSizeHint() const override {
        auto size = QLabel::minimumSizeHint();
        size.setWidth(0);
        return size;
    }
};
} // namespace
NativeViewWidget::NativeViewWidget(QWidget* parent)
    : QWidget(parent), codeFont_(QFontDatabase::systemFont(QFontDatabase::FixedFont)),
      layout_(new QVBoxLayout(this)) {
    setObjectName("nativeExtensionView");
    auto bodyFont = qApp->font();
    bodyFont.setWeight(QFont::Normal);
    bodyFont.setPixelSize(14);
    setFont(bodyFont);
    // Empty margins still participate in the workbench's shared dock-edge hover.
    setMouseTracking(true);
    setAttribute(Qt::WA_Hover);
    layout_->setContentsMargins(0, 0, 0, 0);
    layout_->setSpacing(0);
    applyTheme();
    auto* loading = new components::IconLabelButton(this);
    loading->setIconLabel("$(loading~spin)");
    loading->setAccessibleName(tr("Loading extension view"));
    loading->setEnabled(false);
    layout_->addStretch();
    layout_->addWidget(loading, 0, Qt::AlignCenter);
    layout_->addStretch();
}
void NativeViewWidget::clear() {
    reconciler_.reset();
    controls_.clear();
    menus_.clear();
    while (auto* item = layout_->takeAt(0)) {
        if (item->widget()) {
            retire(item->widget(), this);
        }
        delete item;
    }
}
void NativeViewWidget::showError(const QString& error) {
    setUpdatesEnabled(true);
    building_ = true;
    clear();
    ++revision_;
    auto* label = new QLabel(error, this);
    label->setTextFormat(Qt::PlainText);
    label->setWordWrap(true);
    label->setMargin(8);
    layout_->addWidget(label);
    layout_->addStretch();
    building_ = false;
}
void NativeViewWidget::setCodeFont(const QFont& font) {
    if (codeFont_ == font)
        return;
    codeFont_ = font;
    for (auto* markdown : findChildren<NativeMarkdownView*>())
        markdown->setCodeFont(font);
    for (const auto& control : controls_)
        if (auto* text = qobject_cast<QPlainTextEdit*>(control.data()); text && text->isReadOnly())
            text->setFont(font);
}
void NativeViewWidget::setContent(const NativeControl& root, quint64 revision) {
    if (reconciler_.unchanged(root, controls_)) {
        revision_ = revision;
        return;
    }
    QHash<QString, NativeViewTextState> textStates;
    QHash<QString, NativeViewScrollState> scrollStates;
    QHash<QString, QPair<int, int>> lineStates;
    for (auto it = controls_.cbegin(); it != controls_.cend(); ++it) {
        if (auto* text = qobject_cast<QTextBrowser*>(it.value().data()))
            textStates.insert(it.key(), NativeViewTextState::capture(text));
        else if (auto* editor = qobject_cast<QPlainTextEdit*>(it.value().data()))
            textStates.insert(it.key(), NativeViewTextState::capture(editor));
        else if (auto* scroll = qobject_cast<QScrollArea*>(it.value().data())) {
            scrollStates.insert(it.key(), NativeViewScrollState::capture(*scroll));
        } else if (auto* input = qobject_cast<QLineEdit*>(it.value().data()))
            lineStates.insert(it.key(), {input->cursorPosition(), input->selectedText().size()});
    }
    QString focused;
    if (auto* widget = focusWidget(); widget && isAncestorOf(widget))
        focused = widget->objectName();
    setUpdatesEnabled(false);
    // Painting belongs to this synchronous update transaction, not the frame
    // revision. A repeated frame can advance the revision without rebuilding.
    const auto resumePainting = qScopeGuard([this] { setUpdatesEnabled(true); });
    building_ = true;
    QVector<QPointer<QWidget>> oldRoots;
    for (int i = 0; i < layout_->count(); ++i)
        if (auto* widget = layout_->itemAt(i)->widget())
            oldRoots.append(widget);
    reconciler_.begin(root, controls_);
    controls_.clear();
    revision_ = revision;
    auto* content = build(root, this);
    synchronizeBox(layout_, {{content, 0}});
    auto unused = reconciler_.finish();
    for (auto it = menus_.begin(); it != menus_.end();)
        if (!controls_.contains(it.key()))
            it = menus_.erase(it);
        else
            ++it;
    QSet<QWidget*> retained;
    for (const auto& widget : controls_)
        if (widget)
            retained.insert(widget.data());
    for (const auto& old : oldRoots)
        if (old && !retained.contains(old.data()) && !unused.contains(old))
            unused.append(old);
    for (const auto& old : unused)
        if (old)
            retire(old, this);
    // Publish the frame only after its controls have their final hit rectangles.
    layout_->activate();
    for (auto it = textStates.cbegin(); it != textStates.cend(); ++it) {
        if (reconciler_.reusedExactly(it.key()))
            continue;
        if (auto* text = qobject_cast<QTextBrowser*>(controls_.value(it.key()).data()))
            it->restore(text);
        else if (auto* editor = qobject_cast<QPlainTextEdit*>(controls_.value(it.key()).data()))
            it->restore(editor);
    }
    building_ = false;
    for (auto it = lineStates.cbegin(); it != lineStates.cend(); ++it)
        if (auto* input = qobject_cast<QLineEdit*>(controls_.value(it.key()).data())) {
            input->setProperty("nativeRestoredSelection", true);
            const int position = qBound(0, it->first, int(input->text().size()));
            if (it->second)
                input->setSelection(qMax(0, position - it->second), it->second);
            else
                input->setCursorPosition(position);
        }
    for (auto it = scrollStates.cbegin(); it != scrollStates.cend(); ++it) {
        if (reconciler_.reusedExactly(it.key()))
            continue;
        if (auto* scroll = qobject_cast<QScrollArea*>(controls_.value(it.key()).data())) {
            const QPointer<QScrollArea> area(scroll);
            const auto state = it.value();
            const auto* transcript = qobject_cast<NativeTranscriptView*>(scroll);
            const auto navigation = transcript ? transcript->navigationEpoch() : 0;
            // Stop the fresh transcript's initial follow-to-bottom before
            // queued Markdown wrapping finishes. Restore again after sizing.
            state.restore(*scroll);
            QTimer::singleShot(0, this, [this, area, state, revision, navigation] {
                QTimer::singleShot(0, this, [this, area, state, revision, navigation] {
                    auto* transcript = qobject_cast<NativeTranscriptView*>(area.data());
                    if (area && revision_ == revision &&
                        (!transcript || transcript->navigationEpoch() == navigation))
                        state.restore(*area);
                });
            });
        }
    }
    if (controls_.value(focused))
        controls_.value(focused)->setFocus(Qt::OtherFocusReason);
}
void NativeViewWidget::publish(const QString& id, const QString& type, const QString& text,
                               int index, bool checked) {
    if (!building_) {
        reconciler_.input({id, type, text, index, checked});
        emit input(revision_, {id, type, text, index, checked});
    }
}
QWidget* NativeViewWidget::build(const NativeControl& c, QWidget* parent) {
    constexpr const char* appearances[] = {
        "default", "card",  "composer",   "muted",        "heading", "transcript", "added",
        "removed", "plain", "hoverGroup", "hoverActions", "surface", "shimmer"};
    bool exact = false;
    QWidget* existing = reconciler_.take(c, parent, controls_, exact);
    if (exact) {
        const auto bind = [this](auto&& self, const NativeControl& node) -> void {
            bindHoverActions(node, controls_.value(node.id));
            for (const auto& child : node.children)
                self(self, child);
        };
        bind(bind, c);
        return existing;
    }
    QWidget* widget = nullptr;
    switch (c.kind) {
    case NativeControlKind::Column:
    case NativeControlKind::Row: {
        widget = existing ? existing
                 : c.appearance == NativeControlAppearance::HoverGroup
                     ? static_cast<QWidget*>(new NativeHoverGroup(parent))
                 : c.kind == NativeControlKind::Row
                     ? static_cast<QWidget*>(new NativeContentRow(parent))
                     : new QWidget(parent);
        // Ancestor appearance must exist before child constructors polish their
        // styles, otherwise composer fields retain standalone input borders.
        widget->setProperty("nativeAppearance", appearances[int(c.appearance)]);
        widget->setProperty("nativeRow", c.kind == NativeControlKind::Row);
        bool flexible = false, trailingActions = false;
        if (c.kind == NativeControlKind::Row && parent->property("nativeAppearance") == "surface")
            for (const auto& child : c.children) {
                trailingActions |= flexible && child.kind == NativeControlKind::Button;
                flexible |= child.stretch > 0;
            }
        widget->setProperty("nativeTrailingActions", trailingActions);
        widget->setAttribute(Qt::WA_StyledBackground);
        auto* layout =
            existing ? static_cast<QBoxLayout*>(existing->layout())
                     : new QBoxLayout(c.kind == NativeControlKind::Row ? QBoxLayout::LeftToRight
                                                                       : QBoxLayout::TopToBottom,
                                      widget);
        layout->setContentsMargins(
            c.contentsMargins.value_or(QMargins(c.margin, c.margin, c.margin, c.margin)));
        layout->setSpacing(c.spacing);
        if (c.kind == NativeControlKind::Row)
            widget->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        else if (c.stretch > 0 && parent->property("nativeTrailingActions").toBool())
            widget->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        QVector<QPair<QWidget*, int>> children;
        for (const auto& child : c.children) {
            auto* content = build(child, widget);
            if (child.kind != NativeControlKind::Dialog)
                children.append({content, child.stretch});
        }
        synchronizeBox(layout, children);
        break;
    }
    case NativeControlKind::Dialog: {
        auto* dialog = new NativeDialog(c.text, parent, this);
        for (const auto& child : c.children)
            dialog->contentLayout()->addWidget(build(child, dialog), child.stretch);
        connect(dialog, &QDialog::rejected, this, [this, id = c.id] { publish(id, "dismissed"); });
        if (c.visible)
            QTimer::singleShot(0, dialog, [this, dialog, id = c.id, revision = revision_] {
                if (revision_ != revision || controls_.value(id) != dialog)
                    return;
                dialog->openCentered();
                for (auto* button : dialog->findChildren<QPushButton*>())
                    if (button->property("uiVariant") == "primary")
                        button->setDefault(true);
                if (auto* input = dialog->findChild<QLineEdit*>()) {
                    input->setFocus(Qt::OtherFocusReason);
                    if (!input->property("nativeRestoredSelection").toBool())
                        input->selectAll();
                }
            });
        widget = dialog;
        break;
    }
    case NativeControlKind::Scroll: {
        QScrollArea* scroll = existing ? static_cast<QScrollArea*>(existing)
                              : c.appearance == NativeControlAppearance::Transcript
                                  ? new NativeTranscriptView(parent)
                                  : new QScrollArea(parent);
        if (auto* transcript = qobject_cast<NativeTranscriptView*>(scroll);
            transcript && !existing) {
            connect(transcript, &NativeTranscriptView::boundaryReached, this,
                    [this, id = c.id](bool start) {
                        publish(id, start ? "scrollStart" : "scrollEnd");
                    });
            connect(transcript, &NativeTranscriptView::endRequested, this,
                    [this, id = c.id] { publish(id, "scrollToEnd"); });
        }
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setProperty("nativeAppearance", appearances[int(c.appearance)]);
        auto* content = build(c.children.first(), scroll->viewport());
        if (scroll->widget() != content) {
            scroll->takeWidget();
            scroll->setWidget(content);
        }
        // QScrollArea::setWidget enables an opaque background on its content.
        // Transcript loading must retain the sidebar surface, not paint a box.
        scroll->widget()->setAutoFillBackground(false);
        scroll->viewport()->setAutoFillBackground(false);
        scroll->setAutoFillBackground(false);
        // Wrap to the viewport, including when a vertical scrollbar appears.
        scroll->widget()->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        TransientScrollBars::install(scroll);
        widget = scroll;
        break;
    }
    case NativeControlKind::Label: {
        QLabel* label = c.appearance == NativeControlAppearance::Shimmer
                            ? new components::ShimmerLabel(c.text, parent)
                        : c.appearance == NativeControlAppearance::Plain ||
                                c.appearance == NativeControlAppearance::Heading
                            ? new components::ElidingLabel(c.text, parent)
                        : parent->property("nativeAppearance") == "card"
                            ? static_cast<QLabel*>(new CardLabel(c.text, parent))
                            : new QLabel(c.text, parent);
        label->setTextFormat(Qt::PlainText);
        label->setWordWrap(c.appearance != NativeControlAppearance::Plain &&
                           c.appearance != NativeControlAppearance::Heading &&
                           c.appearance != NativeControlAppearance::Added &&
                           c.appearance != NativeControlAppearance::Removed &&
                           c.appearance != NativeControlAppearance::Shimmer);
        label->setTextInteractionFlags(c.appearance == NativeControlAppearance::Plain ||
                                               c.appearance == NativeControlAppearance::Shimmer
                                           ? Qt::NoTextInteraction
                                           : Qt::TextSelectableByMouse);
        widget = label;
        break;
    }
    case NativeControlKind::Markdown: {
        auto* text = new NativeMarkdownView(c.stretch == 0, parent);
        text->setFrameShape(QFrame::NoFrame);
        text->document()->setDefaultFont(font());
        text->setOpenLinks(false);
        text->setOpenExternalLinks(false);
        text->setCodeFont(codeFont_);
        text->setMarkdownText(c.text);
        connect(text, &QTextBrowser::anchorClicked, this, [this, id = c.id](const QUrl& url) {
            publish(id, "linkActivated", url.toString());
        });
        TransientScrollBars::install(text);
        widget = text;
        break;
    }
    case NativeControlKind::Button: {
        if (!c.children.isEmpty())
            menus_.insert(c.id, c);
        else
            menus_.remove(c.id);
        constexpr components::ButtonVariant variants[] = {
            components::ButtonVariant::Secondary, components::ButtonVariant::Primary,
            components::ButtonVariant::Ghost, components::ButtonVariant::Danger};
        const auto label = components::IconLabelButton::stripIcons(c.text).trimmed();
        const bool labeledMenu = c.buttonStyle == NativeButtonStyle::Ghost &&
                                 c.appearance == NativeControlAppearance::Default &&
                                 !c.children.isEmpty() && !label.isEmpty();
        QAbstractButton* button;
        if (c.text.contains("$(") || !c.iconPath.isEmpty()) {
            auto* icon = new components::IconLabelButton(parent);
            if (labeledMenu)
                icon->setContentsMargins(8, 0, 8, 0);
            else if (c.appearance == NativeControlAppearance::Plain ||
                     c.appearance == NativeControlAppearance::Muted)
                icon->setContentsMargins(0, 0, 0, 0);
            icon->setIconLabel(c.text);
            icon->setIconSize(QSize(c.iconSize, c.iconSize));
            if (!c.iconPath.isEmpty()) {
                icon->setIcon(QIcon(c.iconPath));
            }
            if (c.value > 0) {
                icon->setIconSize(QSize(c.value, c.value));
            }
            icon->setProperty("uiComponent", "button");
            constexpr const char* names[] = {"secondary", "primary", "ghost", "danger"};
            icon->setProperty("uiVariant", names[int(c.buttonStyle)]);
            button = icon;
        } else if (c.stretch > 0)
            button = new components::ElidingButton(c.text, parent, variants[int(c.buttonStyle)]);
        else
            button = new components::Button(c.text, parent, variants[int(c.buttonStyle)]);
        button->setCursor(Qt::PointingHandCursor);
        button->setAttribute(Qt::WA_Hover);
        button->setProperty("nativeIconOnly",
                            (!c.iconPath.isEmpty() || c.text.contains("$(")) && label.isEmpty());
        button->setProperty("nativeLabeledMenu", labeledMenu);
        button->setProperty("uiDensity", "compact");
        button->setProperty("nativeFill", c.stretch > 0);
        if (c.buttonStyle == NativeButtonStyle::Ghost && c.stretch == 0 && !label.isEmpty())
            for (auto* ancestor = parent; ancestor; ancestor = ancestor->parentWidget())
                if (ancestor->property("nativeAppearance") == "surface") {
                    button->setProperty("uiDisabledHoverFeedback", true);
                    break;
                }
        button->setSizePolicy(c.stretch > 0 ? QSizePolicy::Ignored
                              : parent->property("nativeTrailingActions").toBool()
                                  ? QSizePolicy::Fixed
                                  : QSizePolicy::Preferred,
                              QSizePolicy::Fixed);
        connect(
            button, &QAbstractButton::clicked, this, [this, id = c.id, items = c.items, button] {
                const auto menuControl = menus_.value(id);
                if (!menuControl.children.isEmpty()) {
                    auto* menu = createNativeMenu(menuControl, button, [this](const QString& item) {
                        publish(item, "clicked");
                    });
                    showNativeMenu(*menu, *button, true);
                    return;
                }
                if (!items.isEmpty()) {
                    auto* menu = new components::Menu(button);
                    menu->setAttribute(Qt::WA_DeleteOnClose);
                    for (int index = 0; index < items.size(); ++index) {
                        auto* action = menu->addDescriptiveAction(items[index]);
                        connect(action, &QAction::triggered, this,
                                [this, id, text = items[index], index] {
                                    publish(id, "selected", text, index);
                                });
                    }
                    showNativeMenu(*menu, *button);
                    return;
                }
                for (auto* ancestor = button->parentWidget(); ancestor;
                     ancestor = ancestor->parentWidget())
                    if (auto* scroll = qobject_cast<NativeTranscriptView*>(ancestor)) {
                        scroll->setProperty("nativeActivatedAnchor", id);
                        break;
                    }
                publish(id, "clicked");
            });
        widget = button;
        break;
    }
    case NativeControlKind::Input: {
        auto* input = new components::Input(parent);
        input->setText(c.text);
        input->setReadOnly(c.readOnly);
        input->setEchoMode(c.password ? QLineEdit::Password : QLineEdit::Normal);
        input->setPlaceholderText(c.placeholder);
        connect(input, &QLineEdit::textEdited, this,
                [this, id = c.id](const QString& text) { publish(id, "textChanged", text); });
        connect(input, &QLineEdit::returnPressed, this,
                [this, id = c.id, input] { publish(id, "submitted", input->text()); });
        widget = input;
        break;
    }
    case NativeControlKind::TextEdit: {
        QPlainTextEdit* text = c.stretch > 0 ? new QPlainTextEdit(c.text, parent)
                                             : new NativeTextEdit(c.text, c.readOnly, parent);
        text->setReadOnly(c.readOnly);
        if (auto* editor = qobject_cast<NativeTextEdit*>(text)) {
            for (auto* ancestor = parent; ancestor; ancestor = ancestor->parentWidget())
                if (ancestor->property("nativeAppearance") == "composer") {
                    editor->setVerticalPadding(8);
                    break;
                }
            const auto publishFiles = [this, id = c.id](const QStringList& paths) {
                publish(id, "filesPasted",
                        QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(paths))
                                              .toJson(QJsonDocument::Compact)));
            };
            connect(editor, &NativeTextEdit::filesPasted, this, publishFiles);
            connect(editor, &NativeTextEdit::imagePasted, this,
                    [this, publishFiles](const QImage& image) {
                        if (!pastedImages_.isValid())
                            return;
                        const auto path = pastedImages_.filePath(
                            "pasted-image-" + QUuid::createUuid().toString(QUuid::WithoutBraces) +
                            ".png");
                        if (image.save(path, "PNG"))
                            publishFiles({path});
                    });
        }
        if (c.readOnly)
            text->setFont(codeFont_);
        text->setPlaceholderText(c.placeholder);
        text->installEventFilter(this);
        connect(text, &QPlainTextEdit::textChanged, this,
                [this, id = c.id, text] { publish(id, "textChanged", text->toPlainText()); });
        TransientScrollBars::install(text);
        widget = text;
        break;
    }
    case NativeControlKind::ComboBox:
    case NativeControlKind::ChoiceSlider: {
        auto* combo = existing ? static_cast<components::ComboBox*>(existing)
                      : c.kind == NativeControlKind::ChoiceSlider
                          ? static_cast<components::ComboBox*>(new components::ChoiceSlider(parent))
                          : new components::ComboBox(parent);
        combo->setProperty("litecodeChevronResource", c.appearance == NativeControlAppearance::Plain
                                                          ? ":/icons/chevron-right.svg"
                                                          : ":/icons/chevron-down.svg");
        bool withinComposer = false;
        for (auto* ancestor = parent; ancestor; ancestor = ancestor->parentWidget())
            if (ancestor->property("nativeAppearance") == "composer") {
                combo->setPopupWithinWindow(true);
                withinComposer = true;
                break;
            }
        {
            const QSignalBlocker blockSignals(combo);
            combo->setDisplayItems(c.items);
            combo->setIconSize(QSize(c.iconSize, c.iconSize));
            combo->setChoicePresentation(c.itemIconPaths, c.itemColors);
            combo->setPopupHelp(c.helpText, QUrl(c.helpUrl));
            combo->setCurrentIndex(c.currentIndex);
        }
        if (auto* choice = dynamic_cast<components::ChoiceSlider*>(combo)) {
            QVector<QPair<QWidget*, int>> actions, context;
            for (const auto& child : c.children) {
                const bool selector = child.kind == NativeControlKind::ComboBox;
                auto* host =
                    selector ? choice->contextActionsParent() : choice->leadingActionsParent();
                (selector ? context : actions).append({build(child, host), child.stretch});
            }
            synchronizeBox(choice->leadingActionsLayout(), actions);
            synchronizeBox(choice->contextActionsLayout(), context);
            choice->setDescription(c.text);
            choice->setDefaultIndex(c.value);
        }
        if (c.kind == NativeControlKind::ComboBox)
            combo->setPopupTitle(c.text);
        combo->setPlaceholderText(c.placeholder);
        combo->setMinimumContentsLength(4);
        combo->setSizeAdjustPolicy(c.stretch > 0 ? QComboBox::AdjustToMinimumContentsLengthWithIcon
                                                 : QComboBox::AdjustToContents);
        combo->setSizePolicy(c.stretch > 0 ? QSizePolicy::Ignored : QSizePolicy::Preferred,
                             QSizePolicy::Fixed);
        const bool compactChoice =
            withinComposer && !c.itemIconPaths.isEmpty() &&
            std::any_of(c.items.cbegin(), c.items.cend(),
                        [](const QString& item) { return item.contains('\n'); });
        combo->setIconOnly(compactChoice);
        combo->setMinimumWidth(compactChoice ? 28 : 60);
        if (compactChoice)
            combo->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
        if (!existing)
            connect(combo, &QComboBox::activated, this, [this, id = c.id, combo](int index) {
                publish(id, "selectionChanged", combo->itemText(index), index);
            });
        widget = combo;
        break;
    }
    case NativeControlKind::CheckBox: {
        auto* check = new components::CheckBox(parent);
        check->setText(c.text);
        check->setChecked(c.checked);
        connect(check, &QCheckBox::clicked, this,
                [this, id = c.id](bool checked) { publish(id, "toggled", {}, -1, checked); });
        widget = check;
        break;
    }
    case NativeControlKind::List: {
        auto* list = new components::List(parent);
        if (c.contentsMargins)
            list->setViewportMargins(*c.contentsMargins);
        list->addItems(c.items);
        list->setItemDelegate(new NativeListDelegate(list));
        list->setFrameShape(QFrame::NoFrame);
        list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        list->setTextElideMode(Qt::ElideRight);
        for (int i = 0; i < list->count(); ++i)
            list->item(i)->setSizeHint(QSize(0, 30));
        if (c.stretch == 0) {
            list->setFixedHeight(qMin(8, list->count()) * 30 + 2);
        }
        list->setCurrentRow(c.currentIndex);
        connect(list, &QListWidget::currentRowChanged, this, [this, id = c.id, list](int row) {
            publish(id, "selectionChanged", list->item(row) ? list->item(row)->text() : QString{},
                    row);
        });
        TransientScrollBars::install(list);
        widget = list;
        break;
    }
    case NativeControlKind::Progress: {
        QProgressBar* progress = c.appearance == NativeControlAppearance::Plain
                                     ? new components::CircularProgress(parent)
                                     : new QProgressBar(parent);
        if (auto* ring = dynamic_cast<components::CircularProgress*>(progress))
            ring->setDetails(c.text);
        progress->setRange(0, 100);
        progress->setValue(c.value);
        widget = progress;
        break;
    }
    case NativeControlKind::Image: {
        auto* image = new NativeImageView(c.iconPath, c.text, c.value > 0 ? c.value : 80, parent);
        if (!c.children.isEmpty())
            image->setOverlay(build(c.children.first(), image));
        widget = image;
        break;
    }
    case NativeControlKind::Separator: {
        auto* separator = new QFrame(parent);
        separator->setProperty("uiComponent", "nativeSeparator");
        separator->setFixedHeight(1);
        separator->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        widget = separator;
        break;
    }
    }
    widget->setObjectName(c.id);
    widget->installEventFilter(this);
    widget->setProperty("nativeAppearance", appearances[int(c.appearance)]);
    widget->setProperty("nativeRow", c.kind == NativeControlKind::Row);
    widget->setAttribute(Qt::WA_StyledBackground);
    widget->setEnabled(c.enabled);
    if (c.kind != NativeControlKind::Dialog)
        widget->setVisible(c.visible);
    widget->setToolTip(c.toolTip);
    widget->setAccessibleName((!c.accessibleName.isEmpty()
                                   ? c.accessibleName
                                   : (c.toolTip.isEmpty() ? c.text : c.toolTip))
                                  .left(256));
    if (c.kind == NativeControlKind::Button && c.value > 0) {
        widget->ensurePolished();
        widget->setFixedSize(c.value + 8, c.value + 8);
    }
    bindHoverActions(c, widget);
    controls_.insert(c.id, widget);
    return widget;
}
void NativeViewWidget::bindHoverActions(const NativeControl& node, QWidget* widget) {
    if (!widget || node.appearance != NativeControlAppearance::HoverActions)
        return;
    for (auto* ancestor = widget->parentWidget(); ancestor; ancestor = ancestor->parentWidget())
        if (auto* group = dynamic_cast<NativeHoverGroup*>(ancestor)) {
            group->addActions(widget);
            break;
        }
}
bool NativeViewWidget::updateMenuItem(const QString& id,
                                      const std::function<void(NativeControl&, QAction*)>& update) {
    const auto find = [&](auto&& self, NativeControl& control) -> NativeControl* {
        if (control.id == id)
            return &control;
        for (auto& child : control.children)
            if (auto* found = self(self, child))
                return found;
        return nullptr;
    };
    for (auto it = menus_.begin(); it != menus_.end(); ++it)
        if (auto* item = find(find, it.value())) {
            auto* button = controls_.value(it.key()).data();
            update(*item, button ? button->findChild<QAction*>(id) : nullptr);
            return true;
        }
    return false;
}
void NativeViewWidget::setText(const QString& id, const QString& text) {
    auto* widget = controls_.value(id).data();
    if (!widget) {
        if (updateMenuItem(id, [&](NativeControl& item, QAction* action) {
                item.text = text;
                if (action)
                    action->setText(text);
            }))
            reconciler_.textChanged(id, text);
        return;
    }
    if (auto* input = qobject_cast<QLineEdit*>(widget);
        input && input->echoMode() == QLineEdit::Password) {
        // Secret input is intentionally absent from the reconciler's text model.
        // An explicit clear must still reach the editor when that model is empty.
        const QSignalBlocker block(input);
        input->setText(text);
        return;
    }
    if (!reconciler_.textChanged(id, text))
        return;
    const QSignalBlocker block(widget);
    QVector<QPointer<QScrollArea>> pinned;
    for (auto* ancestor = widget->parentWidget(); ancestor && ancestor != this;
         ancestor = ancestor->parentWidget())
        if (auto* area = qobject_cast<QScrollArea*>(ancestor)) {
            auto* bar = area->verticalScrollBar();
            if (bar->value() >= bar->maximum() - 2)
                pinned.append(area);
        }
    if (!pinned.isEmpty())
        QTimer::singleShot(0, this, [pinned] {
            for (const auto& area : pinned)
                if (area) {
                    auto* transcript = qobject_cast<NativeTranscriptView*>(area.data());
                    if (!transcript || transcript->followingLatest())
                        area->verticalScrollBar()->setValue(area->verticalScrollBar()->maximum());
                }
        });
    if (auto* input = qobject_cast<QLineEdit*>(widget)) {
        if (input->text() != text)
            input->setText(text);
    } else if (auto* browser = qobject_cast<QTextBrowser*>(widget)) {
        const auto state = NativeViewTextState::capture(browser);
        if (auto* markdown = dynamic_cast<NativeMarkdownView*>(browser))
            markdown->setMarkdownText(text);
        else
            browser->document()->setMarkdown(text, QTextDocument::MarkdownNoHTML);
        state.restore(browser);
    } else if (auto* editor = qobject_cast<QPlainTextEdit*>(widget)) {
        if (editor->toPlainText() != text) {
            const auto state = NativeViewTextState::capture(editor);
            editor->setPlainText(text);
            state.restore(editor);
        }
    } else if (auto* label = qobject_cast<QLabel*>(widget))
        label->setText(text);
    else if (auto* icon = qobject_cast<components::IconLabelButton*>(widget))
        icon->setIconLabel(text);
    else if (auto* button = qobject_cast<QAbstractButton*>(widget))
        button->setText(text);
}
void NativeViewWidget::scrollToEnd(const QString& id) {
    if (auto* scroll = qobject_cast<QScrollArea*>(controls_.value(id).data())) {
        scroll->setProperty("nativeActivatedAnchor", {});
        if (auto* transcript = qobject_cast<NativeTranscriptView*>(scroll))
            transcript->restorePosition(scroll->verticalScrollBar()->maximum(), true);
        else
            QTimer::singleShot(0, scroll, [scroll] {
                scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
            });
    }
}
void NativeViewWidget::setControlEnabled(const QString& id, bool enabled) {
    if (!reconciler_.enabledChanged(id, enabled))
        return;
    if (controls_.value(id))
        controls_[id]->setEnabled(enabled);
    else
        updateMenuItem(id, [enabled](NativeControl& item, QAction* action) {
            item.enabled = enabled;
            if (action)
                action->setEnabled(enabled && item.kind == NativeControlKind::Button);
        });
}
bool NativeViewWidget::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::MouseButtonPress &&
        static_cast<QMouseEvent*>(event)->button() == Qt::LeftButton) {
        auto* surface = qobject_cast<QWidget*>(watched);
        // Blank layout surfaces focus the draft without intercepting buttons,
        // selectors, or the text editor's own cursor and selection handling.
        if (surface && surface->metaObject() == &QWidget::staticMetaObject)
            for (auto* ancestor = surface; ancestor && ancestor != this;
                 ancestor = ancestor->parentWidget())
                if (ancestor->property("nativeAppearance") == "composer") {
                    for (auto* editor : ancestor->findChildren<QPlainTextEdit*>())
                        if (editor->isEnabled() && !editor->isReadOnly()) {
                            editor->setFocus(Qt::MouseFocusReason);
                            break;
                        }
                    break;
                }
    }
    if (event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter) &&
            (key->modifiers() & (Qt::ControlModifier | Qt::MetaModifier)))
            if (auto* text = qobject_cast<QPlainTextEdit*>(watched); text && !text->isReadOnly()) {
                publish(text->objectName(), "submitted", text->toPlainText());
                return true;
            }
    }
    return QWidget::eventFilter(watched, event);
}
void NativeViewWidget::applyTheme() {
    const bool dark = qApp->property("litecodeDarkTheme").toBool();
    const auto tokens = Theme::tokens(dark);
    auto style = applyThemeTokens(QStringLiteral(R"QSS(
        QTextBrowser { background: transparent; color: @muted; border: 0; padding: 0; font-weight: 400; }
        QDialog[nativeDialog="true"] { background: @input; color: @foreground;
            border: 1px solid @inputBorder; border-radius: 12px; }
        QDialog[nativeDialog="true"] QAbstractButton[uiVariant="danger"] {
            background: @dangerSurface; color: @error; border: 0;
            border-radius: 10px; padding: 6px 12px;
        }
        QDialog[nativeDialog="true"] QAbstractButton[uiVariant="danger"]:hover {
            background: @dangerHoverSurface;
        }
        QFrame#nativeCodeBlock { background: @input; border: 1px solid @inputBorder; border-radius: 12px; }
        QFrame#nativeCodeBlock QPlainTextEdit { background: transparent; border: 0; padding: 0; }
        QWidget[nativeAppearance="transcript"] QScrollBar:vertical,
        QFrame#nativeCodeBlock QScrollBar:vertical { width: 6px; background: transparent; border: 0; margin: 0; }
        QWidget[nativeAppearance="transcript"] QScrollBar:horizontal,
        QFrame#nativeCodeBlock QScrollBar:horizontal { height: 6px; background: transparent; border: 0; margin: 0; }
        QWidget[nativeAppearance="transcript"] QScrollBar::handle,
        QFrame#nativeCodeBlock QScrollBar::handle { border-radius: 3px; }
        QWidget[nativeAppearance="transcript"] QScrollBar[scrollbarActive="true"]::handle,
        QFrame#nativeCodeBlock QScrollBar[scrollbarActive="true"]::handle { background: @scrollbar; }
        QWidget[nativeAppearance="transcript"] QScrollBar::add-line,
        QWidget[nativeAppearance="transcript"] QScrollBar::sub-line,
        QFrame#nativeCodeBlock QScrollBar::add-line,
        QFrame#nativeCodeBlock QScrollBar::sub-line { width: 0; height: 0; background: transparent; }
        QPlainTextEdit { background: @input; color: @foreground; border: 1px solid @inputBorder;
                         border-radius: 6px; padding: 6px; selection-background-color: @selection; }
        QPlainTextEdit:focus { border-color: @focus; }
        QLabel { background: transparent; color: @foreground; }
        QLabel[uiComponent="choiceTitle"] { color: @focus; font-weight: 600; }
        QLabel[uiComponent="choiceDescription"] { color: @muted; }
        QWidget[nativeAppearance="surface"] { background: @elevated; border: 1px solid @border; border-radius: 10px; }
        QScrollArea[nativeAppearance="transcript"] QWidget[nativeAppearance="surface"] {
            background: @transcriptCardSurface;
        }
        QWidget[nativeAppearance="surface"] QWidget[nativeAppearance="card"] {
            background: @hover; border: 0; border-radius: 8px;
        }
        QWidget[nativeAppearance="surface"] QLabel[nativeAppearance="heading"] {
            font-size: 13px; font-weight: 600;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="secondary"] {
            background: @elevated; color: @foreground; border: 1px solid @border;
            border-radius: 8px; padding: 4px 10px; font-weight: 400;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="secondary"]:hover {
            background: @hover;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="secondary"]:pressed {
            background: @pressed;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="ghost"][nativeIconOnly="false"][nativeFill="false"] {
            border-radius: 8px; padding: 4px 8px;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="ghost"][nativeIconOnly="false"][nativeFill="false"]:hover {
            background: @hover;
        }
        QWidget[nativeAppearance="surface"] QAbstractButton[uiVariant="ghost"][nativeIconOnly="false"][nativeFill="false"]:enabled:pressed {
            background: @pressed;
        }
        QWidget[nativeAppearance="hoverGroup"][nativeRow="true"] { background: transparent; border: 0; }
        QWidget[nativeAppearance="hoverGroup"][nativeRow="true"]:hover { background: @hover; }
        QWidget[nativeAppearance="hoverGroup"][nativeRow="true"] QAbstractButton[uiVariant="ghost"],
        QWidget[nativeAppearance="hoverGroup"][nativeRow="true"] QAbstractButton[uiVariant="ghost"]:hover,
        QWidget[nativeAppearance="hoverGroup"][nativeRow="true"] QAbstractButton[uiVariant="ghost"]:pressed {
            background: transparent; border-color: transparent;
        }
        QWidget[nativeAppearance="card"] { background: @hover; border: 0; border-radius: 18px; }
        QWidget[nativeAppearance="composer"] { background: @input; border: 1px solid @inputBorder; border-radius: 20px; }
        QWidget[nativeAppearance="composer"] QPlainTextEdit { background: transparent; border: 0; padding: 0; }
        QWidget[nativeAppearance="composer"] QComboBox { background: transparent; border: 0; padding: 2px; }
        QWidget[nativeAppearance="transcript"] QAbstractButton[uiVariant="primary"],
        QWidget[nativeAppearance="transcript"] QAbstractButton[uiVariant="primary"]:hover,
        QWidget[nativeAppearance="transcript"] QAbstractButton[uiVariant="primary"]:pressed,
        QWidget[nativeAppearance="composer"] QAbstractButton[uiVariant="primary"],
        QWidget[nativeAppearance="composer"] QAbstractButton[uiVariant="primary"]:hover,
        QWidget[nativeAppearance="composer"] QAbstractButton[uiVariant="primary"]:pressed {
            background: @foreground; color: @input; border: 0;
            border-radius: 14px; padding: 5px 12px;
        }
        QWidget[nativeAppearance="composer"] QAbstractButton[uiVariant="primary"][nativeIconOnly="true"] {
            min-width: 28px; max-width: 28px; min-height: 28px; max-height: 28px;
            border: 0; border-radius: 14px; padding: 0; background: @foreground; color: @input;
        }
        QWidget[nativeAppearance="transcript"] QAbstractButton[uiVariant="primary"]:disabled,
        QWidget[nativeAppearance="composer"] QAbstractButton[uiVariant="primary"]:disabled {
            background: @hover; color: @muted;
        }
        QToolButton#nativeTranscriptLatest { background: @input; border: 1px solid @border; border-radius: 16px; padding: 0; }
        QLabel[nativeAppearance="muted"] { color: @activity; font-size: 12px; }
        QLabel[nativeAppearance="shimmer"] { color: @activity; }
        QToolButton[uiComponent="button"][uiVariant="ghost"] { color: @muted; }
        QToolButton[uiComponent="button"][uiVariant="ghost"]:hover,
        QToolButton[uiComponent="button"][uiVariant="ghost"]:pressed {
            background: transparent; border-color: transparent;
        }
        QAbstractButton[nativeLabeledMenu="true"] { border-radius: 11px; }
        QAbstractButton[nativeLabeledMenu="true"]:enabled:hover,
        QAbstractButton[nativeLabeledMenu="true"]:enabled:pressed {
            background: @hover; border-color: transparent;
        }
        QAbstractButton[uiComponent="button"][nativeAppearance="muted"],
        QAbstractButton[uiComponent="button"][nativeAppearance="muted"]:hover,
        QAbstractButton[uiComponent="button"][nativeAppearance="muted"]:pressed {
            color: @activity; background: transparent; border-color: transparent; padding: 0;
        }
        QLabel[nativeAppearance="plain"] { color: @inactiveTabForeground; }
        QAbstractButton[uiComponent="button"][nativeAppearance="plain"],
        QAbstractButton[uiComponent="button"][nativeAppearance="plain"]:hover,
        QAbstractButton[uiComponent="button"][nativeAppearance="plain"]:pressed {
            color: @inactiveTabForeground; background: transparent; border-color: transparent; padding: 0;
        }
        QAbstractButton[uiComponent="button"][nativeImageAction="true"] {
            min-height: 0; background: @foreground; color: @input;
            border: 1px solid @input; border-radius: 8px; padding: 0;
        }
        QAbstractButton[uiComponent="button"][nativeImageAction="true"]:hover { background: @muted; }
        QAbstractButton[uiComponent="button"][nativeImageAction="true"]:disabled { background: @disabledForeground; }
        QPushButton[nativeFill="true"] { text-align: left; }
        QLabel[nativeAppearance="added"] { color: @gitAdded; }
        QLabel[nativeAppearance="removed"] { color: @gitDeleted; }
        QFrame[uiComponent="nativeSeparator"] { background: @border; border: 0; }
        QListWidget[nativeAppearance="plain"] { background: transparent; color: @muted; border: 0; font-size: 14px; }
        QListWidget[nativeAppearance="plain"]::item { padding: 0; border-radius: 6px; }
        QListWidget[nativeAppearance="muted"] { background: transparent; color: @muted; border: 0; }
        QListWidget[nativeAppearance="muted"]::item { padding: 0; border-radius: 6px; }
        QLabel[nativeAppearance="heading"] { font-weight: 600; font-size: 14px; }
    )QSS"),
                                  tokens);
    style.replace("@transcriptCardSurface",
                  styleColor(dark ? tokens.elevatedSurface : tokens.elevatedSurface.lighter(102)));
    auto activity = tokens.mutedForeground;
    activity.setAlphaF(.5f);
    style.replace("@activity", styleColor(activity));
    auto danger = tokens.error;
    danger.setAlphaF(.12f);
    style.replace("@dangerSurface", styleColor(danger));
    danger.setAlphaF(.20f);
    style.replace("@dangerHoverSurface", styleColor(danger));
    auto family = qApp->font().family();
    family.replace('"', "\\\"");
    style.prepend(QString("QLabel, QAbstractButton, QComboBox, QTextBrowser { font-family: \"%1\"; "
                          "font-weight: 400; }\n")
                      .arg(family));
    if (styleSheet() != style)
        setStyleSheet(style);
    auto colors = palette();
    colors.setColor(QPalette::Window, tokens.sidebarSurface);
    setPalette(colors);
    setAutoFillBackground(true);
}
void NativeViewWidget::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::ApplicationPaletteChange || event->type() == QEvent::StyleChange)
        applyTheme();
}
} // namespace litecode::ui
