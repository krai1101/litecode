#include "ui/extensions/DetachedDocumentWatcher.h"
#include "ui/EditorArea.h"
#include "core/FileSystemPath.h"
#include <QDir>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QFutureWatcher>
#include <QTimer>
#include <QtConcurrentRun>
#include <utility>

namespace litecode::ui {
namespace {
QString existingParent(const QString& path) {
    QDir directory(QFileInfo(path).absolutePath());
    while (!directory.exists()) {
        const auto previous = directory.absolutePath();
        if (!directory.cdUp() || directory.absolutePath() == previous) return {};
    }
    return directory.absolutePath();
}
bool beneath(const QString& file, const QString& directory) {
    const auto normalized = core::normalizedFileSystemPath(file);
    auto parent = core::normalizedFileSystemPath(directory);
    if (!parent.endsWith(QLatin1Char('/'))) parent += QLatin1Char('/');
    return normalized.startsWith(parent, core::fileSystemCaseSensitivity(directory));
}
}
DetachedDocumentWatcher::DetachedDocumentWatcher(EditorArea* editors, QObject* parent)
    : QObject(parent), editors_(editors), watcher_(new QFileSystemWatcher(this)), timer_(new QTimer(this)) {
    timer_->setSingleShot(true);
    timer_->setInterval(150);
    connect(timer_, &QTimer::timeout, this, [this] { flush(); });
    connect(watcher_, &QFileSystemWatcher::fileChanged, this, [this](const QString& path) { changed(path, false); });
    connect(watcher_, &QFileSystemWatcher::directoryChanged, this, [this](const QString& path) { changed(path, true); });
    connect(editors, &EditorArea::documentOpened, this, [this] { refresh(); });
    connect(editors, &EditorArea::documentClosed, this, [this](core::DocumentId id) {
        pending_.remove(id.value());
        repeat_.remove(id.value());
        refresh();
    });
    connect(editors, &EditorArea::documentPathChanged, this, [this] { refresh(); });
    connect(editors, &EditorArea::documentSaved, this, [this] { refresh(); });
    connect(editors, &EditorArea::openFilesChanged, this, [this] { refresh(); });
    refresh();
}
void DetachedDocumentWatcher::refresh() {
    QSet<QString> files, directories;
    const auto previousFiles = watcher_->files();
    if (editors_) for (const auto& model : editors_->documentModels().models()) {
        const auto& session = model->session();
        if (model->resource().scheme() != QStringLiteral("file") ||
            session.state() == editor::DocumentState::Loading || editors_->hasDocumentEditor(session.id())) continue;
        const auto path = session.filePath();
        if (QFileInfo::exists(path)) {
            files.insert(path);
            // Closing a tab transfers watch ownership. Check for a disk change
            // in the hand-off interval rather than waiting for another event.
            if (!previousFiles.contains(path)) schedule(session.id());
        }
        const auto parent = existingParent(path);
        if (!parent.isEmpty()) directories.insert(parent);
    }
    const auto reconcile = [this](const QStringList& paths, const QSet<QString>& desired) {
        const QSet<QString> current(paths.cbegin(), paths.cend());
        for (const auto& path : current - desired) watcher_->removePath(path);
        for (const auto& path : desired - current) watcher_->addPath(path);
    };
    reconcile(watcher_->files(), files);
    reconcile(watcher_->directories(), directories);
}
void DetachedDocumentWatcher::changed(const QString& path, bool directory) {
    if (!editors_) return;
    for (const auto& model : editors_->documentModels().models()) {
        const auto& session = model->session();
        if (model->resource().scheme() != QStringLiteral("file") ||
            session.state() == editor::DocumentState::Loading || editors_->hasDocumentEditor(session.id())) continue;
        if ((!directory && core::pathsReferToSameEntry(session.filePath(), path)) ||
            (directory && beneath(session.filePath(), path)))
            schedule(session.id());
    }
    // Atomic replacement can remove a native file watch. The directory watch
    // remains responsible for restoring it, including after delete/recreate.
    refresh();
}
void DetachedDocumentWatcher::schedule(core::DocumentId id) {
    if (running_.contains(id.value())) repeat_.insert(id.value());
    else pending_.insert(id.value());
    timer_->start();
}
void DetachedDocumentWatcher::flush() {
    const auto pending = std::exchange(pending_, {});
    for (const auto value : pending) reload(core::DocumentId(value));
}
void DetachedDocumentWatcher::reload(core::DocumentId id) {
    if (!editors_) return;
    const auto model = editors_->documentModels().find(id);
    if (!model || model->resource().scheme() != QStringLiteral("file") || editors_->hasDocumentEditor(id) ||
        model->session().state() == editor::DocumentState::Loading) return;
    const auto path = model->session().filePath();
    if (!QFileInfo::exists(path)) {
        // Do not discard references or unsaved contents on an external delete.
        model->session().markDeleted();
        refresh();
        return;
    }
    if (model->session().isModified()) {
        model->session().markRestored();
        return; // never replace unsaved work with an external read
    }
    running_.insert(id.value());
    const auto version = model->session().version();
    const auto preferred = model->session().preferredEncoding();
    const auto encoding = model->session().defaultEncoding();
    const auto guess = editors_->documentAutoGuessEncoding();
    auto* watcher = new QFutureWatcher<editor::DocumentLoadResult>(this);
    connect(watcher, &QFutureWatcher<editor::DocumentLoadResult>::finished, this,
        [this, watcher, id, path, version] {
            const QPointer<DetachedDocumentWatcher> alive(this);
            running_.remove(id.value());
            const bool repeat = repeat_.remove(id.value());
            try {
                const auto load = watcher->result();
                const auto model = editors_ ? editors_->documentModels().find(id) : nullptr;
                if (model && model->session().filePath() == path && !editors_->hasDocumentEditor(id) &&
                    !model->session().isModified() && model->session().version() == version) {
                    if (load.outcome == editor::DocumentLoadResult::Outcome::Loaded) {
                        if (!editor::DocumentSession::matchesDiskSnapshot(path, load)) schedule(id);
                        else if (model->session().isDeleted() || model->contents() != load.contents ||
                            model->session().encoding() != load.encoding || model->session().isReadOnly() != load.readOnly ||
                            model->session().hasByteOrderMark() != load.hasByteOrderMark ||
                            model->session().hadDecodingErrors() != load.hadDecodingErrors ||
                            model->session().lineEnding() != load.lineEnding)
                            editors_->acceptDocumentLoad(id, load, nullptr);
                    } else if (!QFileInfo::exists(path)) model->session().markDeleted();
                    else emit editors_->fileReloadFailed(path, load.diagnostic);
                }
            } catch (...) { qWarning("Detached document reload failed."); }
            // A document/error listener may tear down the workbench while a
            // reload announces its result. Do not touch destroyed QObject state.
            if (!alive) return;
            watcher->deleteLater();
            refresh();
            if (repeat) schedule(id);
        });
    watcher->setFuture(QtConcurrent::run([path, preferred, encoding, guess] {
        return editor::DocumentSession::loadBounded(path, preferred, encoding, guess);
    }));
}
} // namespace litecode::ui
