#include "ui/QuickOpenDialog.h"
#include "ui/PopupPositioner.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include "ui/components/PopupSurface.h"
#include "ui/components/IconLabelButton.h"

#include "workspace/WorkspaceSearch.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QCheckBox>
#include <QSignalBlocker>
#include <QPointer>
#include <QStyle>

#include <algorithm>

namespace litecode::ui {
namespace {
constexpr int popupWidth = 636;
constexpr int popupEdgeGap = 16;
constexpr int popupTopGap = 24;
constexpr int resultRowHeight = 23;
constexpr int maximumVisibleRows = 12;
} // namespace

QuickOpenDialog::QuickOpenDialog(QString workspaceRoot, workspace::WorkspaceSearch& search,
                                 QWidget* parent)
    : QDialog(parent), workspaceRoot_(std::move(workspaceRoot)), search_(&search) {
    initialize(tr("Quick Open"), tr("Type a file name"));
    connect(search_, &workspace::WorkspaceSearch::resultBatchReady, this,
            [this](const workspace::SearchResultBatch& batch) { showResults(batch.results); });
    refresh({});
}

QuickOpenDialog::QuickOpenDialog(const QString& title, const QString& placeholder,
                                 QStringList items, QWidget* parent)
    : QDialog(parent), staticItems_(std::move(items)) {
    initialize(title, placeholder);
    refresh({});
}

QuickOpenDialog::QuickOpenDialog(const QString& title, const QString& placeholder,
                                 QVector<QuickPickItem> items, QString initialItemId,
                                 QWidget* parent)
    : QDialog(parent), quickPickItems_(std::move(items)), initialItemId_(std::move(initialItemId)) {
    initialize(title, placeholder);
    refresh({});
}

QString QuickOpenDialog::selectedItemId() const { return selectedItemId_; }
QStringList QuickOpenDialog::selectedItemIds() const {
    if (!many_) return selectedItemId_.isEmpty() ? QStringList{} : QStringList{selectedItemId_};
    QStringList result;
    for (const auto& item : quickPickItems_) if (!item.separator && checkedIds_.contains(item.id)) result.append(item.id);
    return result;
}
void QuickOpenDialog::configureExtensionPicker(bool many, bool description, bool detail, const QString& prompt) {
    extensionPicker_ = true; many_ = many; matchDescription_ = description; matchDetail_ = detail;
    title_->setVisible(!title_->text().isEmpty());
    prompt_->setText(prompt); prompt_->setVisible(!prompt.isEmpty());
    refresh(currentQuery_);
}
void QuickOpenDialog::setPickerItems(QVector<QuickPickItem> items) {
    quickPickItems_ = std::move(items); checkedIds_.clear();
    if (many_) for (const auto& item : quickPickItems_) if (!item.separator && item.picked) checkedIds_.insert(item.id);
    setBusy(false); refresh(currentQuery_);
}
void QuickOpenDialog::setBusy(bool busy) {
    busyState_ = busy; busy_->setVisible(busy); updatePopupGeometry();
}

void QuickOpenDialog::initialize(const QString& title, const QString& placeholder) {
    setObjectName(QStringLiteral("quickOpenDialog"));
    setWindowTitle(title);
    PopupSurface::configureDialog(*this);
    setFixedWidth(popupWidth);
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    surface_ = new PopupSurface(this, 0.55);
    auto* layout = surface_->contentLayout();
    title_ = new QLabel(title, surface_);
    title_->setTextFormat(Qt::PlainText); title_->setAlignment(Qt::AlignCenter); title_->hide();
    layout->addWidget(title_);
    query_ = new components::Input(surface_);
    query_->setObjectName(QStringLiteral("quickOpenQuery"));
    query_->setPlaceholderText(placeholder);
    query_->installEventFilter(this);
    results_ = new components::List(surface_);
    results_->installEventFilter(this);
    results_->setObjectName(QStringLiteral("quickOpenResults"));
    TransientScrollBars::install(results_);
    layout->addWidget(query_);
    prompt_ = new QLabel(surface_); prompt_->setTextFormat(Qt::PlainText); prompt_->setWordWrap(true); prompt_->hide();
    busy_ = new QLabel(tr("Loading…"), surface_); busy_->hide();
    layout->addWidget(prompt_); layout->addWidget(busy_);
    layout->addSpacing(4);
    layout->addWidget(results_);
    outerLayout->addWidget(surface_);
    connect(query_, &QLineEdit::textChanged, this, &QuickOpenDialog::refresh);
    connect(query_, &QLineEdit::returnPressed, this, &QuickOpenDialog::activate);
    connect(results_, &QListWidget::itemClicked, this, &QuickOpenDialog::activate);
    connect(results_, &QListWidget::itemActivated, this, [this] {
        if (!extensionPicker_ || !many_) activate();
    });
    connect(results_, &QListWidget::currentItemChanged, this, [this](QListWidgetItem* item) {
        if (extensionPicker_ && item && (item->flags() & Qt::ItemIsSelectable)) emit activeItemChanged(item->data(Qt::UserRole).toString());
    });
}

void QuickOpenDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    updatePopupGeometry();
    query_->setFocus(Qt::PopupFocusReason);
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event) {
    if ((watched == query_ || (extensionPicker_ && watched == results_)) && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (extensionPicker_ && watched == results_ &&
            (key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter)) {
            activate(); return true;
        }
        if (extensionPicker_ && many_ && watched == results_ && key->key() == Qt::Key_Space) {
            toggleCurrentItem(); return true;
        }
        if (key->key() == Qt::Key_Down && results_->count() > 0) {
            for (int row = results_->currentRow() + 1; row < results_->count(); ++row)
                if (results_->item(row)->flags() & Qt::ItemIsSelectable) { results_->setCurrentRow(row); break; }
            return true;
        }
        if (key->key() == Qt::Key_Up && results_->count() > 0) {
            for (int row = results_->currentRow() - 1; row >= 0; --row)
                if (results_->item(row)->flags() & Qt::ItemIsSelectable) { results_->setCurrentRow(row); break; }
            return true;
        }
        if (extensionPicker_ && many_ && key->key() == Qt::Key_Space && key->modifiers() == Qt::ControlModifier) {
            toggleCurrentItem(); return true;
        }
        if (key->key() == Qt::Key_Escape) {
            reject();
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void QuickOpenDialog::refresh(const QString& query) {
    currentQuery_ = query;
    if (search_ != nullptr)
        search_->findFiles(workspaceRoot_, query, 300);
    else
        showStaticResults(query);
}

void QuickOpenDialog::showStaticResults(const QString& query) {
    if (extensionPicker_) { showExtensionResults(query); return; }
    results_->clear();
    int initialRow = -1;
    const QString trimmedQuery = query.trimmed();
    for (const QuickPickItem& value : quickPickItems_) {
        const QString searchable =
            value.label + QLatin1Char(' ') + value.description + QLatin1Char(' ') + value.id;
        if (!trimmedQuery.isEmpty() && !searchable.contains(trimmedQuery, Qt::CaseInsensitive))
            continue;
        const QString text = value.description.isEmpty()
                                 ? value.label
                                 : QStringLiteral("%1    %2").arg(value.label, value.description);
        auto* item = new QListWidgetItem(text, results_);
        item->setData(Qt::UserRole, value.id);
        if (initialRow < 0 && value.id == initialItemId_)
            initialRow = results_->row(item);
    }
    for (const QString& value : staticItems_) {
        if (!trimmedQuery.isEmpty() && !value.contains(trimmedQuery, Qt::CaseInsensitive))
            continue;
        auto* item = new QListWidgetItem(value, results_);
        item->setData(Qt::UserRole, value);
    }
    if (results_->count() > 0) {
        results_->setCurrentRow(initialRow >= 0 ? initialRow : 0);
        results_->scrollToItem(results_->currentItem());
    }
    updatePopupGeometry();
}
void QuickOpenDialog::showExtensionResults(const QString& query) {
    const auto previous = results_->currentItem() ? results_->currentItem()->data(Qt::UserRole).toString() : QString{};
    QSignalBlocker blocked(results_);
    results_->clear();
    const auto pattern = query.trimmed().toCaseFolded();
    const auto matches = [&](const QString& text) {
        const auto candidate = components::IconLabelButton::stripIcons(text).toCaseFolded();
        qsizetype next = 0;
        for (const auto character : pattern) { next = candidate.indexOf(character, next); if (next < 0) return false; ++next; }
        return true;
    };
    const QuickPickItem* separator = nullptr;
    int activeRow = -1, firstRow = -1;
    for (const auto& value : quickPickItems_) {
        if (value.separator) { separator = &value; continue; }
        if (!value.alwaysShow && !matches(value.label) && !(matchDescription_ && matches(value.description)) && !(matchDetail_ && matches(value.detail))) continue;
        if (separator) {
            auto* heading = new QListWidgetItem(separator->label, results_); heading->setFlags(Qt::NoItemFlags);
            separator = nullptr;
        }
        auto* item = new QListWidgetItem(results_); item->setData(Qt::UserRole, value.id);
        item->setData(Qt::AccessibleTextRole, components::IconLabelButton::stripIcons(value.label));
        auto* row = new QWidget(results_); auto* lines = new QVBoxLayout(row); lines->setContentsMargins(4, 0, 4, 0); lines->setSpacing(0);
        auto* headline = new QHBoxLayout; headline->setContentsMargins(0, 0, 0, 0); headline->setSpacing(6);
        if (many_) {
            auto* check = new components::CheckBox(row); check->setObjectName("quickPickCheck"); check->setAccessibleName(components::IconLabelButton::stripIcons(value.label));
            check->setFocusPolicy(Qt::NoFocus);
            check->setChecked(checkedIds_.contains(value.id));
            connect(check, &QCheckBox::toggled, this, [this, id = value.id](bool checked) { if (checked) checkedIds_.insert(id); else checkedIds_.remove(id); });
            headline->addWidget(check);
        }
        if (!value.icon.isNull()) { auto* icon = new QLabel(row); icon->setPixmap(value.icon.pixmap(16, 16)); headline->addWidget(icon); }
        const auto label = [&](const QString& text, const char* role) {
            auto* widget = new components::IconLabelButton(row); widget->setProperty("uiComponent", role); widget->setIconLabel(text);
            widget->style()->unpolish(widget); widget->style()->polish(widget);
            widget->setAttribute(Qt::WA_TransparentForMouseEvents); widget->setFocusPolicy(Qt::NoFocus); return widget;
        };
        headline->addWidget(label(value.label, "pickerLabel"));
        if (!value.description.isEmpty()) headline->addWidget(label(value.description, "pickerDetail"), 1);
        else headline->addStretch();
        lines->addLayout(headline);
        if (!value.detail.isEmpty()) lines->addWidget(label(value.detail, "pickerDetail"));
        item->setSizeHint(QSize(0, value.detail.isEmpty() ? resultRowHeight : resultRowHeight * 2));
        results_->setItemWidget(item, row);
        if (firstRow < 0) firstRow = results_->row(item);
        if (value.id == previous) activeRow = results_->row(item);
    }
    results_->setCurrentRow(activeRow >= 0 ? activeRow : firstRow);
    updatePopupGeometry();
    // Signals are blocked while rebuilding; emit only the final active item.
    blocked.unblock();
    if (const auto* current = results_->currentItem()) emit activeItemChanged(current->data(Qt::UserRole).toString());
}

void QuickOpenDialog::showResults(const QVector<workspace::SearchResult>& results) {
    results_->clear();
    for (const auto& result : results) {
        auto* item = new QListWidgetItem(results_);
        item->setData(Qt::UserRole, result.filePath);
        auto* label = new QLabel(results_);
        label->setObjectName(QStringLiteral("quickOpenResult"));
        const QString escaped = result.preview.toHtmlEscaped();
        QString highlighted = escaped;
        const qsizetype match = result.preview.indexOf(currentQuery_, 0, Qt::CaseInsensitive);
        if (!currentQuery_.isEmpty() && match >= 0) {
            const QString before = result.preview.left(match).toHtmlEscaped();
            const QString hit = result.preview.mid(match, currentQuery_.size()).toHtmlEscaped();
            const QString after = result.preview.mid(match + currentQuery_.size()).toHtmlEscaped();
            highlighted = before + QStringLiteral("<b>%1</b>").arg(hit) + after;
        }
        label->setText(highlighted);
        label->setTextFormat(Qt::RichText);
        label->setAttribute(Qt::WA_TransparentForMouseEvents);
        item->setSizeHint(QSize(0, resultRowHeight));
        results_->setItemWidget(item, label);
    }
    if (results_->count()) {
        results_->setCurrentRow(0);
    }
    updatePopupGeometry();
}

void QuickOpenDialog::updatePopupGeometry() {
    const int visibleRows = std::clamp(results_->count(), 1, maximumVisibleRows);
    int listHeight = visibleRows * resultRowHeight;
    if (extensionPicker_ && results_->count()) {
        listHeight = 0;
        for (int row = 0; row < visibleRows; ++row)
            listHeight += std::max(resultRowHeight, results_->item(row)->sizeHint().height());
    }
    results_->setFixedHeight(listHeight + 2);
    QWidget* owner = parentWidget() ? parentWidget()->window() : nullptr;
    setFixedWidth(PopupPositioner::boundedWidth(owner, popupWidth, popupEdgeGap));
    if (layout() != nullptr)
        layout()->activate();
    if (surface_->layout() != nullptr)
        surface_->layout()->activate();
    int popupHeight = surface_->sizeHint().height() + 16;
    if (owner) {
        const int maximumHeight =
            std::max(resultRowHeight + 48, owner->height() - popupTopGap - popupEdgeGap);
        if (popupHeight > maximumHeight) {
            results_->setFixedHeight(
                std::max(resultRowHeight + 2, results_->height() - (popupHeight - maximumHeight)));
            if (surface_->layout() != nullptr)
                surface_->layout()->activate();
            popupHeight = surface_->sizeHint().height() + 16;
        }
    }
    setFixedHeight(popupHeight);
    PopupPositioner::placeTopCentered(*this, owner, popupTopGap, popupEdgeGap);
}

void QuickOpenDialog::activate() {
    if (extensionPicker_ && busyState_) return;
    if (const auto* item = results_->currentItem()) {
        if (!(item->flags() & Qt::ItemIsSelectable)) return;
        if (extensionPicker_ && many_ && sender() == results_) {
            toggleCurrentItem();
            return;
        }
        selectedItemId_ = item->data(Qt::UserRole).toString();
        const QPointer<QuickOpenDialog> self(this);
        emit fileSelected(selectedItemId_);
        if (self) accept();
    } else if (extensionPicker_ && many_) {
        accept();
    }
}

void QuickOpenDialog::toggleCurrentItem() {
    auto* item = results_->currentItem();
    if (!item || !(item->flags() & Qt::ItemIsSelectable) || busyState_) return;
    auto* row = results_->itemWidget(item);
    if (auto* check = row ? row->findChild<QCheckBox*>() : nullptr) check->toggle();
}

} // namespace litecode::ui
