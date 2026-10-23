#include "ui/QuickOpenDialog.h"
#include "ui/PopupPositioner.h"
#include "ui/TransientScrollBars.h"
#include "ui/components/Controls.h"
#include "ui/components/PopupSurface.h"

#include "workspace/WorkspaceSearch.h"

#include <QFileInfo>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QShowEvent>
#include <QVBoxLayout>

#include <algorithm>

namespace litecode::ui {
namespace {
constexpr int popupWidth = 636;
constexpr int popupEdgeGap = 16;
constexpr int popupTopGap = 30;
constexpr int resultRowHeight = 27;
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

void QuickOpenDialog::initialize(const QString& title, const QString& placeholder) {
    setObjectName(QStringLiteral("quickOpenDialog"));
    setWindowTitle(title);
    PopupSurface::configureDialog(*this);
    setFixedWidth(popupWidth);
    auto* outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(8, 8, 8, 8);
    surface_ = new PopupSurface(this);
    auto* layout = surface_->contentLayout();
    query_ = new components::Input(surface_);
    query_->setObjectName(QStringLiteral("quickOpenQuery"));
    query_->setPlaceholderText(placeholder);
    query_->installEventFilter(this);
    results_ = new components::List(surface_);
    results_->setObjectName(QStringLiteral("quickOpenResults"));
    TransientScrollBars::install(results_);
    layout->addWidget(query_);
    layout->addWidget(results_);
    outerLayout->addWidget(surface_);
    connect(query_, &QLineEdit::textChanged, this, &QuickOpenDialog::refresh);
    connect(query_, &QLineEdit::returnPressed, this, &QuickOpenDialog::activate);
    connect(results_, &QListWidget::itemClicked, this, &QuickOpenDialog::activate);
    connect(results_, &QListWidget::itemActivated, this, &QuickOpenDialog::activate);
}

void QuickOpenDialog::showEvent(QShowEvent* event) {
    QDialog::showEvent(event);
    updatePopupGeometry();
    query_->setFocus(Qt::PopupFocusReason);
}

bool QuickOpenDialog::eventFilter(QObject* watched, QEvent* event) {
    if (watched == query_ && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        if (key->key() == Qt::Key_Down && results_->count() > 0) {
            results_->setCurrentRow(std::min(results_->currentRow() + 1, results_->count() - 1));
            return true;
        }
        if (key->key() == Qt::Key_Up && results_->count() > 0) {
            results_->setCurrentRow(std::max(results_->currentRow() - 1, 0));
            return true;
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
        item->setSizeHint(QSize(0, 27));
        results_->setItemWidget(item, label);
    }
    if (results_->count()) {
        results_->setCurrentRow(0);
    }
    updatePopupGeometry();
}

void QuickOpenDialog::updatePopupGeometry() {
    const int visibleRows = std::clamp(results_->count(), 1, maximumVisibleRows);
    results_->setFixedHeight(visibleRows * resultRowHeight + 2);
    QWidget* owner = parentWidget();
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
    if (const auto* item = results_->currentItem()) {
        selectedItemId_ = item->data(Qt::UserRole).toString();
        emit fileSelected(selectedItemId_);
        accept();
    }
}

} // namespace litecode::ui
