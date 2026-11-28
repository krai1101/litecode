#include "ui/extensions/WorkbenchWorkspaceEditService.h"
#include "extensions/DocumentTextBuffer.h"
#include "ui/extensions/WorkspaceEditReviewService.h"
#include <QFutureWatcher>
#include <limits>
namespace litecode::ui {
WorkbenchWorkspaceEditService::WorkbenchWorkspaceEditService(EditorArea* editors,
                                                             WorkbenchDocumentOpenService* opens,
                                                             QObject* parent)
    : QObject(parent), editors_(editors), opens_(opens) {
    const auto stopped = [this] {
        const QPointer<WorkbenchWorkspaceEditService> alive(this);
        stopped_ = true;
        const auto ids = operations_.keys();
        for (const auto id : ids) {
            finish(id, false);
            if (!alive)
                return;
        }
    };
    connect(editors, &QObject::destroyed, this, stopped);
    connect(opens, &QObject::destroyed, this, stopped);
}
WorkbenchWorkspaceEditService::~WorkbenchWorkspaceEditService() {
    stopped_ = true;
    const auto ids = operations_.keys();
    for (const auto id : ids)
        finish(id, false);
}
void WorkbenchWorkspaceEditService::setReviewService(WorkspaceEditReviewService* reviews) {
    if (reviews_ == reviews)
        return;
    const QPointer<WorkbenchWorkspaceEditService> alive(this);
    const auto ids = operations_.keys();
    for (const auto id : ids) {
        const auto operation = operations_.value(id);
        if (operation && operation->needsReview)
            finish(id, false);
        if (!alive)
            return;
    }
    reviews_ = reviews;
}
QFuture<bool>
WorkbenchWorkspaceEditService::apply(const QString& owner,
                                     const extensions::WorkspaceEditRequest& request) {
    auto operation = std::make_shared<Operation>();
    operation->promise = std::make_shared<QPromise<bool>>();
    operation->promise->start();
    const auto future = operation->promise->future();
    const auto fail = [&] {
        operation->promise->addResult(false);
        operation->promise->finish();
        return future;
    };
    if (stopped_ || !editors_ || !opens_ || request.entries.isEmpty() ||
        nextId_ == std::numeric_limits<quint64>::max())
        return fail();
    operation->owner = owner;
    operation->versions = request.versions;
    for (const auto& entry : request.entries) {
        const auto* text = std::get_if<extensions::detail::WorkspaceTextEntry>(&entry);
        // File/notebook operations still require their own atomic backend.
        // They must never be silently dropped from an approved text batch.
        if (!text)
            return fail();
        if (text->metadata) {
            operation->needsReview |= text->metadata->needsConfirmation;
            auto& annotations = operation->annotations[text->uri.toString(QUrl::FullyEncoded)];
            const auto annotation =
                text->metadata->label +
                (text->metadata->description ? " — " + *text->metadata->description : QString());
            if (!annotation.isEmpty() && !annotations.contains(annotation))
                annotations.append(annotation);
        }
        auto found = std::find_if(operation->groups.begin(), operation->groups.end(),
                                  [&](const auto& group) {
                                      return group.first.toString(QUrl::FullyEncoded) ==
                                             text->uri.toString(QUrl::FullyEncoded);
                                  });
        if (found == operation->groups.end())
            operation->groups.append({text->uri, {text->edit}});
        else
            found->second.append(text->edit);
    }
    if (operation->needsReview && !reviews_)
        return fail();
    const auto id = ++nextId_;
    operation->referenceOwner = QStringLiteral("$workspace-edit:%1").arg(id);
    operations_.insert(id, operation);
    next(id);
    return future;
}
void WorkbenchWorkspaceEditService::next(quint64 id) {
    const auto operation = operations_.value(id);
    if (!operation)
        return;
    if (!editors_ || !opens_) {
        finish(id, false);
        return;
    }
    if (operation->index == operation->groups.size()) {
        if (operation->needsReview) {
            review(id);
            return;
        }
        const QPointer<WorkbenchWorkspaceEditService> alive(this);
        const bool result = editors_->applyDocumentEditPlans(operation->plans);
        if (alive)
            finish(id, result);
        return;
    }
    const auto uri = operation->groups[operation->index].first;
    auto* watcher = new QFutureWatcher<extensions::TextDocumentSnapshot>(this);
    connect(watcher, &QFutureWatcher<extensions::TextDocumentSnapshot>::finished, this,
            [this, watcher, id, uri] {
                const auto operation = operations_.value(id);
                watcher->deleteLater();
                if (!operation || !editors_)
                    return;
                try {
                    const auto snapshot = watcher->result();
                    const auto model = editors_->documentModels().find(
                        core::DocumentId(snapshot.id.toULongLong()));
                    const auto key = uri.toString(QUrl::FullyEncoded);
                    if (!model || model->session().isReadOnly() || !model->hasTextBuffer() ||
                        (operation->versions.contains(key) &&
                         operation->versions.value(key) != model->session().version() + 1)) {
                        finish(id, false);
                        return;
                    }
                    extensions::DocumentTextBuffer buffer(QString::fromUtf8(model->contents()),
                                                          snapshot.eol);
                    QVector<core::DocumentEdit> edits;
                    std::optional<core::LineEnding> newEol;
                    for (const auto& edit : operation->groups[operation->index].second) {
                        if (edit.newEol) {
                            newEol = *edit.newEol == extensions::EndOfLine::CRLF
                                         ? core::LineEnding::CrLf
                                         : core::LineEnding::Lf;
                            if (edit.range.isEmpty() && edit.newText.isEmpty())
                                continue;
                        }
                        if (!buffer.validate(edit.range.start()).isEqual(edit.range.start()) ||
                            !buffer.validate(edit.range.end()).isEqual(edit.range.end())) {
                            finish(id, false);
                            return;
                        }
                        const auto start = buffer.offsetAt(edit.range.start()),
                                   end = buffer.offsetAt(edit.range.end());
                        core::DocumentEdit converted;
                        const auto text = buffer.text();
                        converted.position = text.first(start).toUtf8().size();
                        converted.removedLength = text.mid(start, end - start).toUtf8().size();
                        auto insertion = edit.newText;
                        insertion.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
                        insertion.replace(QLatin1Char('\r'), QLatin1Char('\n'));
                        if (snapshot.eol == extensions::EndOfLine::CRLF)
                            insertion.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
                        converted.insertedText = insertion.toUtf8();
                        edits.append(converted);
                    }
                    auto plan = std::find_if(
                        operation->plans.begin(), operation->plans.end(),
                        [&](const auto& plan) { return plan.id == model->session().id(); });
                    if (plan == operation->plans.end())
                        operation->plans.append({model->session().id(), model->session().version(),
                                                 std::move(edits), newEol});
                    else {
                        if (plan->expectedVersion != model->session().version()) {
                            finish(id, false);
                            return;
                        }
                        plan->edits.append(edits);
                        if (newEol)
                            plan->newEol = newEol;
                    }
                    ++operation->index;
                    next(id);
                } catch (...) {
                    finish(id, false);
                }
            });
    try {
        watcher->setFuture(opens_->open(uri, {}, operation->referenceOwner));
    } catch (...) {
        watcher->deleteLater();
        finish(id, false);
    }
}
void WorkbenchWorkspaceEditService::review(quint64 id) {
    const auto operation = operations_.value(id);
    if (!operation || !editors_ || !reviews_) {
        finish(id, false);
        return;
    }
    QVector<WorkspaceEditReviewDocument> documents;
    for (const auto& plan : operation->plans) {
        const auto model = editors_->documentModels().find(plan.id);
        if (!model) {
            finish(id, false);
            return;
        }
        auto preview = *model;
        auto edits = plan.edits;
        if (!preview.applyEdits(plan.expectedVersion, &edits, false)) {
            finish(id, false);
            return;
        }
        std::optional<core::DocumentEdit> eol;
        if (plan.newEol &&
            !preview.replaceWithLineEnding(preview.contents(), *plan.newEol,
                                           core::DocumentEditReason::Other, &eol, nullptr)) {
            finish(id, false);
            return;
        }
        documents.append(
            {model->resource(), model->contents(), preview.contents(),
             operation->annotations.value(model->resource().toString(QUrl::FullyEncoded))});
    }
    auto* watcher = new QFutureWatcher<bool>(this);
    connect(watcher, &QFutureWatcher<bool>::finished, this, [this, watcher, id] {
        watcher->deleteLater();
        const auto operation = operations_.value(id);
        if (!operation || !editors_)
            return;
        bool accepted = false;
        try {
            accepted = watcher->result();
        } catch (...) {
        }
        const QPointer<WorkbenchWorkspaceEditService> alive(this);
        // Approval is tied to these exact plans. Editing a document while the
        // preview was open invalidates its expectedVersion; never rebase unseen.
        const bool applied = accepted && editors_->applyDocumentEditPlans(operation->plans);
        if (alive)
            finish(id, applied);
    });
    watcher->setFuture(reviews_->review(id, operation->owner, std::move(documents)));
}
void WorkbenchWorkspaceEditService::finish(quint64 id, bool applied) {
    const auto operation = operations_.take(id);
    if (!operation)
        return;
    if (reviews_)
        reviews_->cancel(id);
    if (opens_)
        opens_->removeOwner(operation->referenceOwner);
    operation->promise->addResult(applied);
    operation->promise->finish();
}
void WorkbenchWorkspaceEditService::removeOwner(const QString& owner) {
    const QPointer<WorkbenchWorkspaceEditService> alive(this);
    const auto ids = operations_.keys();
    for (const auto id : ids) {
        const auto operation = operations_.value(id);
        if (operation && operation->owner == owner)
            finish(id, false);
        if (!alive)
            return;
    }
}
} // namespace litecode::ui
