#include "ui/SettingsDialog.h"

#include "core/SettingsService.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include "ui/styles/SettingsDialogVisuals.h"

#include <QAbstractItemView>
#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QEvent>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QHideEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidgetItem>
#include <QPainter>
#include <QPointer>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSpinBox>
#include <QStackedWidget>
#include <QStyle>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

namespace litecode::ui {
namespace {

constexpr int NavigationTargetRole = Qt::UserRole + 1;
constexpr int EncodingSelectWidth = 400;

class EncodingItemDelegate final : public QStyledItemDelegate {
  public:
    using QStyledItemDelegate::QStyledItemDelegate;

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        QStyleOptionViewItem background(option);
        initStyleOption(&background, index);
        const QString label = background.text;
        background.text.clear();
        const QStyle* style = background.widget ? background.widget->style() : qApp->style();
        style->drawControl(QStyle::CE_ItemViewItem, &background, painter, background.widget);

        painter->save();
        const bool selected = option.state.testFlag(QStyle::State_Selected);
        painter->setPen(selected ? option.palette.color(QPalette::HighlightedText)
                                 : option.palette.color(QPalette::Text));
        const QRect textRect = option.rect.adjusted(8, 0, -8, 0);
        painter->drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, label);
        const int keyLeft = textRect.left() + option.fontMetrics.horizontalAdvance(label) + 8;
        const QRect keyRect(keyLeft, textRect.top(), qMax(0, textRect.right() - keyLeft + 1),
                            textRect.height());
        painter->setPen(selected ? option.palette.color(QPalette::HighlightedText)
                                 : option.palette.color(QPalette::PlaceholderText));
        painter->drawText(keyRect, Qt::AlignLeft | Qt::AlignVCenter,
                          option.fontMetrics.elidedText(index.data(Qt::UserRole).toString(),
                                                        Qt::ElideRight, keyRect.width()));
        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex& index) const override {
        QSize size = QStyledItemDelegate::sizeHint(option, index);
        size.setHeight(qMax(size.height(), 27));
        return size;
    }
};

QLabel* textLabel(const QString& text, const QString& role, QWidget* parent) {
    auto* label = new QLabel(text, parent);
    label->setProperty("uiRole", role);
    label->setWordWrap(true);
    label->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    label->setMinimumWidth(0);
    label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    return label;
}

QString sectionKey(const settings::CategoryDefinition& category,
                   const settings::SectionDefinition& section) {
    return category.id + QLatin1Char('/') + section.id;
}

} // namespace

SettingsDialog::SettingsDialog(const QFont& editorFont, bool autoGuessEncoding,
                               core::TextEncoding fileEncoding, QWidget* parent)
    : QDialog(parent), editorFont_(editorFont), savedEditorFont_(editorFont),
      savedAutoGuessEncoding_(autoGuessEncoding), savedFileEncoding_(fileEncoding) {
    setObjectName(QStringLiteral("settingsDialog"));
    setWindowTitle(tr("Settings"));

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addWidget(new DialogHeader(tr("Settings"), *this, this));

    auto* searchRegion = new QWidget(this);
    searchRegion->setObjectName(QStringLiteral("settingsSearchRegion"));
    auto* searchLayout = new QVBoxLayout(searchRegion);
    searchLayout->setContentsMargins(30, 16, 30, 14);
    auto* search = new components::Input(searchRegion);
    search->setObjectName(QStringLiteral("settingsSearch"));
    search->setPlaceholderText(tr("Search settings"));
    search->setClearButtonEnabled(true);
    searchLayout->addWidget(search);
    layout->addWidget(searchRegion);

    userEncodingError_ =
        textLabel(tr("Settings contain an unsupported Files: Encoding value. UTF-8 is used "
                     "until that value is corrected."),
                  QStringLiteral("workspaceError"), this);
    userEncodingError_->setObjectName(QStringLiteral("userEncodingError"));
    layout->addWidget(userEncodingError_);
    userEncodingError_->hide();
    auto* body = new QWidget(this);
    body->setObjectName(QStringLiteral("settingsBody"));
    auto* bodyLayout = new QHBoxLayout(body);
    bodyLayout->setContentsMargins(30, 0, 30, 0);
    bodyLayout->setSpacing(0);

    navigation_ = new components::List(body);
    navigation_->setObjectName(QStringLiteral("settingsNavigation"));
    navigation_->setFixedWidth(220);
    navigation_->setFrameShape(QFrame::NoFrame);
    navigation_->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    navigation_->setSelectionMode(QAbstractItemView::SingleSelection);
    bodyLayout->addWidget(navigation_);

    auto* separator = new QFrame(body);
    separator->setObjectName(QStringLiteral("settingsNavigationSeparator"));
    separator->setFrameShape(QFrame::VLine);
    bodyLayout->addWidget(separator);

    pages_ = new QStackedWidget(body);
    pages_->setObjectName(QStringLiteral("settingsPages"));
    buildSettingsTree(pages_);
    bodyLayout->addWidget(pages_, 1);
    layout->addWidget(body, 1);

    autoGuess_->setChecked(autoGuessEncoding);
    const int encodingIndex = fileEncoding_->findData(core::textEncodingKey(fileEncoding));
    fileEncoding_->setCurrentIndex(encodingIndex >= 0 ? encodingIndex : 0);

    connect(search, &QLineEdit::textChanged, this, &SettingsDialog::filterSettings);
    connect(navigation_, &QListWidget::itemClicked, this,
            [this](QListWidgetItem* item) { showNavigationItem(item); });
    connect(autoGuess_, &QCheckBox::toggled, this, [this] {
        emit valuesChanged();
        emit settingChanged(QStringLiteral("files.autoGuessEncoding"));
    });
    connect(fileEncoding_, &QComboBox::currentIndexChanged, this, [this] {
        emit valuesChanged();
        emit settingChanged(QStringLiteral("files.encoding"));
    });
    connect(this, &SettingsDialog::settingChanged, this, &SettingsDialog::updatePendingChange);
    auto* findShortcut = new QShortcut(QKeySequence::Find, this);
    connect(findShortcut, &QShortcut::activated, search, [search] {
        search->setFocus();
        search->selectAll();
    });

    DialogChrome::configure(*this, DialogMode::Modeless);
    DialogChrome::fitToOwner(*this, QSize(1120, 720), QSize(720, 500));
    search->setFocus();
}

QFont SettingsDialog::editorFont() const { return editorFont_; }

bool SettingsDialog::autoGuessEncoding() const { return autoGuess_->isChecked(); }

bool SettingsDialog::hasPendingChanges() const {
    return !changedSettings_.isEmpty() || !resetSettings_.isEmpty();
}

QSet<QString> SettingsDialog::changedSettings() const { return changedSettings_; }

QSet<QString> SettingsDialog::resetSettings() const { return resetSettings_; }

void SettingsDialog::setUserEncodingError(bool error) { userEncodingError_->setVisible(error); }

core::TextEncoding SettingsDialog::fileEncoding() const {
    return core::textEncodingFromKey(fileEncoding_->currentData().toString())
        .value_or(core::TextEncoding::Utf8);
}

void SettingsDialog::setValues(const QFont& editorFont, bool autoGuessEncoding,
                               core::TextEncoding fileEncoding) {
    setDraftValues(editorFont, autoGuessEncoding, fileEncoding);
    savedEditorFont_ = editorFont;
    savedAutoGuessEncoding_ = autoGuessEncoding;
    savedFileEncoding_ = fileEncoding;
    changedSettings_.clear();
    resetSettings_.clear();
}

void SettingsDialog::setDraftValues(const QFont& editorFont, bool autoGuessEncoding,
                                    core::TextEncoding fileEncoding) {
    const QSignalBlocker familyBlocker(editorFontFamily_);
    const QSignalBlocker weightBlocker(editorFontWeight_);
    const QSignalBlocker sizeBlocker(editorFontSize_);
    const QSignalBlocker guessBlocker(autoGuess_);
    const QSignalBlocker encodingBlocker(fileEncoding_);
    editorFont_ = editorFont;
    int familyIndex = editorFontFamily_->findText(editorFont.family());
    if (familyIndex < 0) {
        editorFontFamily_->addItem(editorFont.family());
        familyIndex = editorFontFamily_->count() - 1;
    }
    editorFontFamily_->setCurrentIndex(familyIndex);
    const int weightIndex = editorFontWeight_->findData(static_cast<int>(editorFont.weight()));
    editorFontWeight_->setCurrentIndex(weightIndex >= 0 ? weightIndex : 0);
    editorFontSize_->setValue(editorFont.pixelSize());
    autoGuess_->setChecked(autoGuessEncoding);
    fileEncoding_->setCurrentIndex(fileEncoding_->findData(core::textEncodingKey(fileEncoding)));
}

void SettingsDialog::updatePendingChange(const QString& id) {
    resetSettings_.remove(id);
    bool changed = false;
    if (id == QStringLiteral("editor.fontFamily"))
        changed = editorFont_.family() != savedEditorFont_.family();
    else if (id == QStringLiteral("editor.fontWeight"))
        changed = editorFont_.weight() != savedEditorFont_.weight();
    else if (id == QStringLiteral("editor.fontSize"))
        changed = editorFont_.pixelSize() != savedEditorFont_.pixelSize();
    else if (id == QStringLiteral("files.autoGuessEncoding"))
        changed = autoGuessEncoding() != savedAutoGuessEncoding_;
    else if (id == QStringLiteral("files.encoding"))
        changed = fileEncoding() != savedFileEncoding_;
    if (changed)
        changedSettings_.insert(id);
    else
        changedSettings_.remove(id);
}

void SettingsDialog::stageReset(const QString& id) {
    QFont font = editorFont_;
    bool autoGuess = autoGuessEncoding();
    core::TextEncoding encoding = fileEncoding();
    if (id == QStringLiteral("editor.fontFamily"))
        font.setFamily(core::SettingsService::defaultEditorFontFamily());
    else if (id == QStringLiteral("editor.fontWeight"))
        font.setWeight(QFont::Normal);
    else if (id == QStringLiteral("editor.fontSize"))
        font.setPixelSize(core::SettingsService::defaultEditorFontSize());
    else if (id == QStringLiteral("files.autoGuessEncoding"))
        autoGuess = false;
    else if (id == QStringLiteral("files.encoding"))
        encoding = core::TextEncoding::Utf8;
    else
        return;
    setDraftValues(font, autoGuess, encoding);
    changedSettings_.remove(id);
    resetSettings_.insert(id);
    emit resetRequested(id);
}

void SettingsDialog::refreshTheme() { applySettingsDialogVisuals(*this); }

bool SettingsDialog::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() != QEvent::MouseButtonPress || !isVisible())
        return QDialog::eventFilter(watched, event);

    auto* target = qobject_cast<QWidget*>(watched);
    if (!target)
        return QDialog::eventFilter(watched, event);
    if (target->window() == this) {
        for (components::ComboBox* combo : {editorFontFamily_, editorFontWeight_, fileEncoding_}) {
            if (combo->popupWidget()->isVisible() && target != combo)
                combo->popupWidget()->hide();
        }
        return QDialog::eventFilter(watched, event);
    }
    for (components::ComboBox* combo : {editorFontFamily_, editorFontWeight_, fileEncoding_}) {
        if (combo->popupWidget() == target->window())
            return QDialog::eventFilter(watched, event);
    }

    const QPointer<SettingsDialog> guard(this);
    QTimer::singleShot(0, this, [guard] {
        if (guard)
            guard->close();
    });
    return true;
}

void SettingsDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    qApp->installEventFilter(this);
}

void SettingsDialog::hideEvent(QHideEvent* event) {
    qApp->removeEventFilter(this);
    QDialog::hideEvent(event);
}

QWidget* SettingsDialog::createEditor(const settings::Definition& definition, QWidget* parent) {
    switch (definition.editorKind) {
    case settings::EditorKind::FontFamily: {
        editorFontFamily_ = new components::ComboBox(parent);
        editorFontFamily_->setObjectName(QStringLiteral("editorFontFamily"));
        editorFontFamily_->setMinimumContentsLength(34);
        editorFontFamily_->addItems(QFontDatabase::families());
        int currentIndex = editorFontFamily_->findText(editorFont_.family());
        if (currentIndex < 0) {
            editorFontFamily_->addItem(editorFont_.family());
            currentIndex = editorFontFamily_->count() - 1;
        }
        editorFontFamily_->setCurrentIndex(currentIndex);
        connect(editorFontFamily_, &QComboBox::currentTextChanged, this,
                [this](const QString& family) {
                    const QString selectedFamily = family.trimmed();
                    if (selectedFamily.isEmpty() || selectedFamily == editorFont_.family())
                        return;
                    editorFont_.setFamily(selectedFamily);
                    emit valuesChanged();
                    emit settingChanged(QStringLiteral("editor.fontFamily"));
                });
        return editorFontFamily_;
    }
    case settings::EditorKind::FontWeight: {
        editorFontWeight_ = new components::ComboBox(parent);
        editorFontWeight_->setObjectName(QStringLiteral("editorFontWeight"));
        editorFontWeight_->addItem(tr("Normal"), static_cast<int>(QFont::Normal));
        editorFontWeight_->addItem(tr("Semi Bold"), static_cast<int>(QFont::DemiBold));
        editorFontWeight_->addItem(tr("Bold"), static_cast<int>(QFont::Bold));
        const int currentIndex =
            editorFontWeight_->findData(static_cast<int>(editorFont_.weight()));
        editorFontWeight_->setCurrentIndex(currentIndex >= 0 ? currentIndex : 0);
        connect(editorFontWeight_, &QComboBox::currentIndexChanged, this, [this](int index) {
            const int weight = editorFontWeight_->itemData(index).toInt();
            if (weight == editorFont_.weight())
                return;
            editorFont_.setWeight(static_cast<QFont::Weight>(weight));
            emit valuesChanged();
            emit settingChanged(QStringLiteral("editor.fontWeight"));
        });
        return editorFontWeight_;
    }
    case settings::EditorKind::FontSize:
        editorFontSize_ = new QSpinBox(parent);
        editorFontSize_->setObjectName(QStringLiteral("editorFontSize"));
        editorFontSize_->setRange(6, 72);
        editorFontSize_->setButtonSymbols(QAbstractSpinBox::NoButtons);
        editorFontSize_->setValue(qBound(6, editorFont_.pixelSize(), 72));
        connect(editorFontSize_, &QSpinBox::valueChanged, this, [this](int size) {
            if (size == editorFont_.pixelSize())
                return;
            editorFont_.setPixelSize(size);
            emit valuesChanged();
            emit settingChanged(QStringLiteral("editor.fontSize"));
        });
        return editorFontSize_;
    case settings::EditorKind::Boolean:
        autoGuess_ = new QCheckBox(parent);
        autoGuess_->setAccessibleName(definition.title);
        return autoGuess_;
    case settings::EditorKind::Encoding:
        fileEncoding_ = new components::ComboBox(parent);
        fileEncoding_->setObjectName(QStringLiteral("fileEncoding"));
        fileEncoding_->setFixedWidth(EncodingSelectWidth);
        fileEncoding_->setMinimumContentsLength(34);
        fileEncoding_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
        fileEncoding_->setPopupItemDelegate(new EncodingItemDelegate(fileEncoding_));
        for (const core::TextEncoding encoding : core::supportedTextEncodings()) {
            fileEncoding_->addItem(core::textEncodingLongDisplayName(encoding),
                                   core::textEncodingKey(encoding));
            fileEncoding_->setItemData(fileEncoding_->count() - 1, core::textEncodingKey(encoding),
                                       Qt::ToolTipRole);
        }
        return fileEncoding_;
    }
    return nullptr;
}

QWidget* SettingsDialog::createSettingRow(const settings::Definition& definition, QWidget* parent) {
    auto* row = new QWidget(parent);
    row->setObjectName(QStringLiteral("settingRow"));
    row->setProperty("settingId", definition.id);
    auto* reset = new components::IconButton(row);
    reset->setText(tr("Reset"));
    reset->setToolTip(tr("Restore default for %1").arg(definition.title));
    reset->setAccessibleName(reset->toolTip());
    connect(reset, &QToolButton::clicked, this, [this, id = definition.id] { stageReset(id); });
    if (definition.editorKind == settings::EditorKind::Boolean) {
        auto* layout = new QHBoxLayout(row);
        layout->setContentsMargins(10, 10, 10, 14);
        layout->setSpacing(10);
        QWidget* editor = createEditor(definition, row);
        editor->setProperty("settingEditor", true);
        layout->addWidget(editor, 0, Qt::AlignTop);
        auto* labels = new QWidget(row);
        auto* labelsLayout = new QVBoxLayout(labels);
        labelsLayout->setContentsMargins(0, 0, 0, 0);
        labelsLayout->setSpacing(7);
        auto* titleLayout = new QHBoxLayout;
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->addWidget(textLabel(definition.title, QStringLiteral("settingTitle"), labels));
        titleLayout->addWidget(reset);
        labelsLayout->addLayout(titleLayout);
        labelsLayout->addWidget(
            textLabel(definition.description, QStringLiteral("description"), labels));
        layout->addWidget(labels, 1);
    } else {
        auto* layout = new QVBoxLayout(row);
        layout->setContentsMargins(10, 10, 10, 14);
        layout->setSpacing(7);
        auto* titleLayout = new QHBoxLayout;
        titleLayout->setContentsMargins(0, 0, 0, 0);
        titleLayout->addWidget(textLabel(definition.title, QStringLiteral("settingTitle"), row));
        titleLayout->addWidget(reset);
        layout->addLayout(titleLayout);
        layout->addWidget(textLabel(definition.description, QStringLiteral("description"), row));
        if (QWidget* editor = createEditor(definition, row)) {
            editor->setProperty("settingEditor", true);
            layout->addWidget(editor, 0, Qt::AlignLeft);
        }
    }
    return row;
}

void SettingsDialog::buildSettingsTree(QWidget* content) {
    for (const settings::CategoryDefinition& categoryDefinition : settings::categories()) {
        for (const settings::SectionDefinition& sectionDefinition : categoryDefinition.sections) {
            const QString key = sectionKey(categoryDefinition, sectionDefinition);
            auto* sectionItem = new QListWidgetItem(sectionDefinition.title, navigation_);
            sectionItem->setData(NavigationTargetRole, key);
            sectionItem->setToolTip(
                tr("%1: %2").arg(categoryDefinition.title, sectionDefinition.title));
            sectionItems_.insert(key, sectionItem);

            auto* scrollArea = new QScrollArea(content);
            scrollArea->setObjectName(QStringLiteral("settingsScrollArea"));
            scrollArea->setFrameShape(QFrame::NoFrame);
            scrollArea->setWidgetResizable(true);
            scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

            auto* page = new QWidget(scrollArea);
            page->setObjectName(QStringLiteral("settingsContent"));
            page->setProperty("settingsSectionKey", key);
            auto* pageLayout = new QVBoxLayout(page);
            pageLayout->setContentsMargins(46, 6, 34, 36);
            pageLayout->setSpacing(0);
            pageLayout->addWidget(
                textLabel(categoryDefinition.title, QStringLiteral("categoryTitle"), page));
            pageLayout->addWidget(
                textLabel(sectionDefinition.title, QStringLiteral("sectionTitle"), page));

            for (const settings::Definition& definition : sectionDefinition.settings) {
                QWidget* row = createSettingRow(definition, page);
                pageLayout->addWidget(row);
                QStringList searchable{definition.id, categoryDefinition.title,
                                       sectionDefinition.title, definition.title,
                                       definition.description};
                searchable.append(definition.keywords);
                renderedSettings_.append(
                    {definition.id, key, searchable.join(QLatin1Char(' ')).toCaseFolded(), row});
            }
            pageLayout->addStretch(1);
            scrollArea->setWidget(page);
            sectionPageIndexes_.insert(key, pages_->addWidget(scrollArea));
        }
    }

    emptyPage_ = new QWidget(content);
    emptyPage_->setObjectName(QStringLiteral("settingsEmptyPage"));
    auto* emptyLayout = new QVBoxLayout(emptyPage_);
    emptyState_ = textLabel(tr("No settings found"), QStringLiteral("emptyState"), emptyPage_);
    emptyState_->setAlignment(Qt::AlignCenter);
    emptyLayout->addStretch(1);
    emptyLayout->addWidget(emptyState_);
    emptyLayout->addStretch(2);
    pages_->addWidget(emptyPage_);

    if (navigation_->count() > 0)
        showNavigationItem(navigation_->item(0));
}

void SettingsDialog::filterSettings(const QString& query) {
    const QString needle = query.trimmed().toCaseFolded();
    QHash<QString, bool> visibleSections;
    bool hasVisibleSetting = false;
    for (const RenderedSetting& setting : renderedSettings_) {
        const bool visible = needle.isEmpty() || setting.searchableText.contains(needle);
        setting.row->setVisible(visible);
        visibleSections[setting.sectionKey] = visibleSections.value(setting.sectionKey) || visible;
        hasVisibleSetting = hasVisibleSetting || visible;
    }

    for (auto it = sectionItems_.cbegin(); it != sectionItems_.cend(); ++it) {
        const bool visible = visibleSections.value(it.key());
        if (QListWidgetItem* item = it.value())
            item->setHidden(!visible);
    }
    if (!hasVisibleSetting) {
        navigation_->setCurrentItem(nullptr);
        pages_->setCurrentWidget(emptyPage_);
        return;
    }

    QListWidgetItem* current = navigation_->currentItem();
    if (current && !current->isHidden()) {
        showNavigationItem(current);
        return;
    }
    for (int index = 0; index < navigation_->count(); ++index) {
        QListWidgetItem* item = navigation_->item(index);
        if (!item->isHidden()) {
            showNavigationItem(item);
            return;
        }
    }
}

void SettingsDialog::showNavigationItem(QListWidgetItem* item) {
    if (!item)
        return;
    const QString key = item->data(NavigationTargetRole).toString();
    const auto page = sectionPageIndexes_.constFind(key);
    if (page == sectionPageIndexes_.cend())
        return;
    navigation_->setCurrentItem(item);
    pages_->setCurrentIndex(page.value());
}

} // namespace litecode::ui
