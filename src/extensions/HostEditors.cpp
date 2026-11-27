#include "extensions/HostEditors.h"
#include "extensions/HostTextEditorEdit.h"
#include <QMutexLocker>

namespace litecode::extensions {
namespace {
QFuture<bool> result(bool value) {
    QPromise<bool> promise; promise.start(); const auto future = promise.future();
    promise.addResult(value); promise.finish(); return future;
}
bool sameSelections(const QVector<Selection>& left, const QVector<Selection>& right) {
    if (left.size() != right.size()) return false;
    for (qsizetype index = 0; index < left.size(); ++index)
        if (!left[index].anchor().isEqual(right[index].anchor()) || !left[index].active().isEqual(right[index].active())) return false;
    return true;
}
}
class HostEditors::Editor final : public TextEditor {
  public:
    Editor(QString id, QString documentId, std::shared_ptr<HostTextDocument> document, HostWorkspaceRequests* requests)
        : id_(std::move(id)), documentId_(std::move(documentId)), document_(std::move(document)), requests_(requests) {}
    std::shared_ptr<TextDocument> document() const override { return document_; }
    Selection selection() const override { QMutexLocker lock(&mutex_); return selections_.first(); }
    QVector<Selection> selections() const override { QMutexLocker lock(&mutex_); return selections_; }
    QFuture<bool> edit(const EditCallback& callback, TextEditorEditOptions options) override {
        // Collection stays synchronous and preserves callback errors as rejected
        // futures. The captured document version, not this view's last version,
        // is used to validate the eventual workbench transaction.
        try {
            {
                QMutexLocker lock(&mutex_);
                if (disposed_ || document_->isClosed()) throw std::runtime_error("TextEditor#edit not possible on closed editors.");
            }
            const auto edit = HostTextEditorEdit::collect(document_, callback, options.undoStopBefore, options.undoStopAfter);
            if (edit.empty()) return result(true);
            QMutexLocker lock(&mutex_);
            if (!visible_ || document_->isClosed() || !requests_) return result(false);
            return requests_->applyEditorEdit(id_, edit);
        } catch (...) {
            QPromise<bool> promise; promise.start(); const auto future = promise.future();
            promise.setException(std::current_exception()); promise.finish(); return future;
        }
    }
    bool update(const QVector<Selection>& selections, bool visible) {
        QMutexLocker lock(&mutex_);
        const bool changed = !sameSelections(selections_, selections);
        selections_ = selections; visible_ = visible; return changed;
    }
    void detach() { QMutexLocker lock(&mutex_); visible_ = false; }
    void dispose() { QMutexLocker lock(&mutex_); visible_ = false; disposed_ = true; }
    const QString& documentId() const { return documentId_; }
  private:
    QString id_, documentId_;
    std::shared_ptr<HostTextDocument> document_;
    QPointer<HostWorkspaceRequests> requests_;
    mutable QMutex mutex_;
    QVector<Selection> selections_{Selection(Position(0, 0), Position(0, 0))};
    bool visible_{};
    bool disposed_{};
};
HostEditors::HostEditors(HostDocuments& documents, HostWorkspaceRequests& requests) : documents_(&documents), requests_(&requests) {}
HostEditors::~HostEditors() { shutdown(); }
std::shared_ptr<TextEditor> HostEditors::activeTextEditor() const {
    QMutexLocker lock(&mutex_);
    return activeId_ ? editors_.value(*activeId_) : std::shared_ptr<TextEditor>{};
}
QVector<std::shared_ptr<TextEditor>> HostEditors::visibleTextEditors() const {
    QMutexLocker lock(&mutex_);
    QVector<std::shared_ptr<TextEditor>> result;
    for (const auto& id : visibleIds_) result.append(editors_.value(id));
    return result;
}
bool HostEditors::accept(const WindowEditorState& state, bool initial) {
    QVector<std::shared_ptr<HostTextDocument>> documents;
    if (!documents_ || !requests_) return false;
    for (const auto& item : state.editors) {
        auto document = std::dynamic_pointer_cast<HostTextDocument>(documents_->find(item.documentId));
        if (!document || document->isClosed() || document->version() != item.documentVersion || item.selections.isEmpty()) return false;
        documents.append(std::move(document));
    }
    QVector<TextEditorSelectionChangeEvent> selectionEvents;
    QVector<std::shared_ptr<TextEditor>> visible;
    std::shared_ptr<TextEditor> active;
    bool activeChanged = false, visibleChanged = false;
    {
        QMutexLocker lock(&mutex_);
        if (stopped_) return false;
        // Validate every binding before mutating any cached facade.
        for (const auto& item : state.editors)
            if (const auto prior = editors_.value(item.id); prior && prior->documentId() != item.documentId) return false;
        QStringList ids;
        for (const auto& item : state.editors) ids.append(item.id);
        for (const auto& id : visibleIds_) if (!ids.contains(id)) editors_.value(id)->detach();
        for (qsizetype index = 0; index < state.editors.size(); ++index) {
            const auto& item = state.editors[index];
            auto editor = editors_.value(item.id);
            if (!editor) { editor = std::make_shared<Editor>(item.id, item.documentId, documents[index], requests_); editors_.insert(item.id, editor); }
            if (editor->update(item.selections, true)) selectionEvents.append({editor, item.selections, {}});
            visible.append(editor);
        }
        activeChanged = activeId_ != state.activeEditorId;
        visibleChanged = visibleIds_ != ids;
        visibleIds_ = std::move(ids); activeId_ = state.activeEditorId;
        if (activeId_) active = editors_.value(*activeId_);
        // Closed document facades remain valid for retained plugin references,
        // but no longer keep the host's registry alive indefinitely.
        for (auto it = editors_.begin(); it != editors_.end();)
            if (!visibleIds_.contains(it.key()) && it.value()->document()->isClosed()) { it.value()->dispose(); it = editors_.erase(it); }
            else ++it;
    }
    if (initial) return true;
    const QPointer<HostEditors> alive(this);
    if (visibleChanged) visibleChanged_.fire(visible);
    if (!alive) return true;
    if (activeChanged) activeChanged_.fire(active);
    if (!alive) return true;
    for (const auto& event : selectionEvents) { selectionChanged_.fire(event); if (!alive) return true; }
    return true;
}
void HostEditors::shutdown() {
    {
        QMutexLocker lock(&mutex_);
        if (stopped_) return;
        stopped_ = true;
        for (const auto& editor : editors_) editor->dispose();
        editors_.clear(); visibleIds_.clear(); activeId_.reset();
    }
    activeChanged_.dispose(); visibleChanged_.dispose(); selectionChanged_.dispose();
}
} // namespace litecode::extensions
