#include "ui/extensions/VirtualDocumentUpdates.h"
#include "editor/DiffModel.h"
#include "ui/EditorArea.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
#include <limits>
namespace litecode::ui {
namespace {
std::optional<core::DocumentEdit> trim(const QString& before, const QString& after) {
    if (before == after)
        return {};
    qsizetype prefix = 0;
    while (prefix < before.size() && prefix < after.size() && before.at(prefix) == after.at(prefix))
        ++prefix;
    if (prefix && prefix < before.size() &&
        (before.at(prefix).isLowSurrogate() ||
         (before.at(prefix) == QLatin1Char('\n') && before.at(prefix - 1) == QLatin1Char('\r'))))
        --prefix;
    qsizetype suffix = 0;
    while (suffix < before.size() - prefix && suffix < after.size() - prefix &&
           before.at(before.size() - suffix - 1) == after.at(after.size() - suffix - 1))
        ++suffix;
    const auto start = before.size() - suffix;
    if (suffix &&
        (before.at(start).isLowSurrogate() || (start && before.at(start) == QLatin1Char('\n') &&
                                               before.at(start - 1) == QLatin1Char('\r'))))
        --suffix;
    const auto end = before.size() - suffix;
    core::DocumentEdit edit;
    edit.position = before.first(prefix).toUtf8().size();
    edit.removedLength = before.mid(prefix, end - prefix).toUtf8().size();
    edit.insertedText = after.mid(prefix, after.size() - suffix - prefix).toUtf8();
    return edit;
}
QVector<core::DocumentEdit> compute(const QString& before, QString after, core::LineEnding eol) {
    after.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    after.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    if (eol == core::LineEnding::CrLf)
        after.replace(QStringLiteral("\n"), QStringLiteral("\r\n"));
    const auto original = before.toUtf8(), modified = after.toUtf8();
    const auto offsets = [](const QByteArray& bytes) {
        QVector<qsizetype> result{0};
        for (qsizetype index = bytes.indexOf('\n'); index >= 0;
             index = bytes.indexOf('\n', index + 1))
            result.append(index + 1);
        if (result.last() != bytes.size())
            result.append(bytes.size());
        return result;
    };
    const auto oldOffsets = offsets(original), newOffsets = offsets(modified);
    QVector<core::DocumentEdit> result;
    for (const auto& hunk : editor::compareLines(original, modified)) {
        const auto oldStart = oldOffsets.at(hunk.originalStart),
                   newStart = newOffsets.at(hunk.modifiedStart);
        const auto oldEnd = oldOffsets.at(hunk.originalStart + hunk.originalCount);
        const auto newEnd = newOffsets.at(hunk.modifiedStart + hunk.modifiedCount);
        if (auto edit = trim(QString::fromUtf8(original.mid(oldStart, oldEnd - oldStart)),
                             QString::fromUtf8(modified.mid(newStart, newEnd - newStart)))) {
            edit->position += oldStart;
            result.append(*edit);
        }
    }
    return result;
}
} // namespace
VirtualDocumentUpdates::VirtualDocumentUpdates(EditorArea* editors,
                                               extensions::WorkbenchContentProviders* providers,
                                               QObject* parent)
    : QObject(parent), editors_(editors) {
    connect(providers, &extensions::WorkbenchContentProviders::virtualDocumentChanged, this,
            [this](const QUrl& uri, const QString& content) { update(uri, content); });
    connect(editors, &EditorArea::documentClosed, this,
            [this](core::DocumentId id) { generations_.remove(id.value()); });
}
void VirtualDocumentUpdates::update(const QUrl& uri, const QString& content) {
    if (!editors_)
        return;
    const auto model = editors_->documentModels().find(uri);
    if (!model || !model->session().isReadOnly() || !model->session().filePath().isEmpty() ||
        model->session().state() == editor::DocumentState::Loading || uri.scheme() == "untitled")
        return;
    if (nextGeneration_ == std::numeric_limits<quint64>::max())
        return;
    const auto generation = ++nextGeneration_;
    const auto id = model->session().id();
    const auto version = model->session().version();
    generations_.insert(id.value(), generation);
    auto* watcher = new QFutureWatcher<QVector<core::DocumentEdit>>(this);
    connect(watcher, &QFutureWatcher<QVector<core::DocumentEdit>>::finished, this,
            [this, watcher, id, uri, version, generation] {
                const QPointer<VirtualDocumentUpdates> alive(this);
                try {
                    if (!editors_ || generations_.value(id.value()) != generation) {
                        watcher->deleteLater();
                        return;
                    }
                    generations_.remove(id.value());
                    const auto model = editors_->documentModels().find(id);
                    if (model && model->resource() == uri &&
                        model->session().version() == version) {
                        editors_->applyVirtualDocumentEdits(id, version, watcher->result());
                    }
                } catch (...) {
                    qWarning("Virtual document update computation failed.");
                }
                if (!alive)
                    return;
                watcher->deleteLater();
            });
    const auto before = QString::fromUtf8(model->contents());
    const auto eol = model->session().lineEnding();
    watcher->setFuture(
        QtConcurrent::run([before, content, eol] { return compute(before, content, eol); }));
}
} // namespace litecode::ui
