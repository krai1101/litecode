#include "ui/extensions/WorkspaceEditReviewDialog.h"
#include "ui/DiffEditorWidget.h"
#include "ui/FileIconTheme.h"
#include "ui/components/Controls.h"
#include "ui/components/DialogChrome.h"
#include "ui/components/DialogHeader.h"
#include <QDialogButtonBox>
#include <QLabel>
#include <QSplitter>
#include <QStackedWidget>
#include <QVBoxLayout>

namespace litecode::ui {
WorkspaceEditReviewDialog::WorkspaceEditReviewDialog(
    const QString& owner, const QVector<WorkspaceEditReviewDocument>& documents, QWidget* parent)
    : QDialog(parent) {
    setObjectName("workspaceEditReviewDialog");
    setAttribute(Qt::WA_DeleteOnClose);
    DialogChrome::configure(*this);
    setWindowTitle(tr("Review Changes"));
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(new DialogHeader(windowTitle(), *this, this));
    auto* source = new QLabel(
        tr("%1 requests changes to %2 document(s). Nothing is applied until you approve.")
            .arg(owner)
            .arg(documents.size()),
        this);
    source->setTextFormat(Qt::PlainText);
    source->setWordWrap(true);
    layout->addWidget(source);
    auto* splitter = new QSplitter(this);
    auto* files = new components::List(splitter);
    files->setObjectName("workspaceEditReviewFiles");
    auto* pages = new QStackedWidget(splitter);
    for (const auto& document : documents) {
        const auto path = document.uri.isLocalFile() ? document.uri.toLocalFile()
                                                     : document.uri.toDisplayString();
        files->addItem(path);
        auto* item = files->item(files->count() - 1);
        item->setIcon(fileIconForPath(path));
        item->setToolTip(path);
        auto* page = new QWidget(pages);
        auto* content = new QVBoxLayout(page);
        content->setContentsMargins(0, 0, 0, 0);
        if (!document.annotations.isEmpty()) {
            auto* annotations = new QLabel(document.annotations.join('\n'), page);
            annotations->setObjectName("workspaceEditReviewAnnotations");
            annotations->setTextFormat(Qt::PlainText);
            annotations->setWordWrap(true);
            content->addWidget(annotations);
        }
        pages->addWidget(page);
    }
    connect(files, &QListWidget::currentRowChanged, pages, [pages, documents](int row) {
        if (row < 0 || row >= documents.size())
            return;
        pages->setCurrentIndex(row);
        auto* page = pages->widget(row);
        if (page->findChild<DiffEditorWidget*>())
            return;
        // Build the expensive diff only when that document is selected.
        const auto& document = documents[row];
        workspace::GitComparison comparison;
        comparison.path = comparison.originalPath = document.uri.isLocalFile()
                                                        ? document.uri.toLocalFile()
                                                        : document.uri.toDisplayString();
        comparison.original = document.before;
        comparison.modified = document.after;
        comparison.staged = comparison.historical = true;
        comparison.originalRevision = tr("Current");
        comparison.modifiedRevision = tr("Preview");
        auto* diff = new DiffEditorWidget(comparison, page);
        // This view is a snapshot, not a live editor navigation surface.
        if (auto* open = diff->findChild<QToolButton*>("openDiffFile"))
            open->hide();
        static_cast<QVBoxLayout*>(page->layout())->addWidget(diff, 1);
    });
    files->setCurrentRow(0);
    splitter->setStretchFactor(1, 1);
    splitter->setSizes({220, 680});
    layout->addWidget(splitter, 1);
    auto* actions = new QDialogButtonBox(this);
    auto* apply = actions->addButton(tr("Apply Changes"), QDialogButtonBox::AcceptRole);
    apply->setObjectName("workspaceEditReviewApply");
    DialogChrome::markPrimary(apply);
    actions->addButton(QDialogButtonBox::Cancel);
    connect(actions, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(actions, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(actions);
    components::adoptDialogControls(*this);
    DialogChrome::fitToOwner(*this, QSize(1000, 700), QSize(600, 400));
}
} // namespace litecode::ui
