#include "terminal/TerminalSessionService.h"

#include "terminal/ITerminalBackend.h"
#include "terminal/TerminalFactory.h"
#include "terminal/TerminalInputQueue.h"

#include <QTimer>

#include <algorithm>

namespace litecode::terminal {

struct TerminalSessionService::Session final {
    core::OperationId id;
    TerminalProfile profile;
    QString workingDirectory;
    TerminalState state{TerminalState::Stopped};
    bool pendingRemoval{false};
    bool removalQueued{false};
    TerminalInputQueue prelaunchInput;
    std::unique_ptr<ITerminalBackend> backend;
};

TerminalSessionService::TerminalSessionService(QObject* parent, BackendFactory factory)
    : QObject(parent), profiles_(availableTerminalProfiles()), backendFactory_(std::move(factory)) {
}

TerminalSessionService::~TerminalSessionService() { requestStopAll(); }

const QVector<TerminalProfile>& TerminalSessionService::profiles() const noexcept {
    return profiles_;
}

QVector<TerminalSessionSnapshot> TerminalSessionService::sessions() const {
    QVector<TerminalSessionSnapshot> result;
    result.reserve(static_cast<qsizetype>(sessions_.size()));
    for (const auto& session : sessions_) {
        result.push_back(
            {session->id, session->profile.id, session->profile.displayName, session->state});
    }
    return result;
}

core::OperationId TerminalSessionService::createSession(const QString& profileId,
                                                        const QString& workingDirectory) {
    const TerminalProfile* requested = findProfile(profileId);
    if (!requested)
        requested = &profiles_.constFirst();

    auto session = std::make_unique<Session>();
    session->id = operationIds_.next();
    session->profile = *requested;
    session->workingDirectory = workingDirectory;
    session->state = TerminalState::Starting;
    session->backend = backendFactory_ ? backendFactory_() : createTerminalBackend();
    Session* created = session.get();
    const core::OperationId id = created->id;

    connect(created->backend.get(), &ITerminalBackend::started, this,
            [this, id](core::OperationId operationId) {
                if (Session* session = find(id)) {
                    while (!session->prelaunchInput.isEmpty()) {
                        const QByteArray bytes = session->prelaunchInput.nextSlice(16 * 1024);
                        if (!session->backend->enqueueInput(bytes))
                            break;
                        session->prelaunchInput.consume(bytes.size());
                    }
                    session->prelaunchInput.clear();
                }
                emit sessionStarted(operationId);
            });
    connect(created->backend.get(), &ITerminalBackend::outputReady, this,
            [this, id](const QByteArray& bytes) { emit outputReady(id, bytes); });
    connect(created->backend.get(), &ITerminalBackend::foregroundProcessChanged, this,
            [this](core::OperationId operationId, const QString& processName) {
                emit foregroundProcessChanged(operationId, processName);
            });
    connect(created->backend.get(), &ITerminalBackend::stateChanged, this,
            [this, id](core::OperationId, TerminalState state) {
                if (Session* session = find(id))
                    session->state = state;
                emit sessionStateChanged(id, state);
            });
    connect(created->backend.get(), &ITerminalBackend::inputRejected, this,
            [this](core::OperationId operationId, const QString& diagnostic) {
                emit inputRejected(operationId, diagnostic);
            });
    connect(created->backend.get(), &ITerminalBackend::errorOccurred, this,
            [this, id](const QString& diagnostic) { emit sessionError(id, diagnostic); });
    connect(created->backend.get(), &ITerminalBackend::completed, this,
            [this, id](const TerminalExit& result) {
                Session* session = find(id);
                if (!session)
                    return;
                session->state = TerminalState::Stopped;
                const bool remove = session->pendingRemoval;
                emit sessionCompleted(result);
                if (remove)
                    queueEraseSession(id);
            });

    const TerminalSessionSnapshot snapshot{id, created->profile.id, created->profile.displayName,
                                           created->state};
    sessions_.push_back(std::move(session));
    emit sessionCreated(snapshot);
    QTimer::singleShot(0, this, [this, id] {
        Session* session = find(id);
        if (!session)
            return;
        if (session->pendingRemoval) {
            queueEraseSession(id);
            return;
        }
        session->backend->start(session->id, session->profile, session->workingDirectory);
    });
    return id;
}

bool TerminalSessionService::sendInput(core::OperationId id, const QByteArray& bytes) {
    Session* session = find(id);
    if (!session) {
        emit inputRejected(id, tr("The terminal session no longer exists."));
        return false;
    }
    if (session->state == TerminalState::Starting) {
        if (session->prelaunchInput.enqueue(bytes))
            return true;
        emit inputRejected(id, tr("Terminal input queue is full."));
        return false;
    }
    return session->backend->enqueueInput(bytes);
}

void TerminalSessionService::resizeSession(core::OperationId id, const QSize& characters) {
    if (Session* session = find(id))
        session->backend->resizeTerminal(characters);
}

void TerminalSessionService::requestRemove(core::OperationId id) {
    Session* session = find(id);
    if (!session || session->pendingRemoval)
        return;
    session->pendingRemoval = true;
    if (session->backend->state() == TerminalState::Stopped ||
        session->backend->state() == TerminalState::Failed) {
        queueEraseSession(id);
        return;
    }
    session->backend->requestStop();
}

void TerminalSessionService::requestStopAll() {
    for (const auto& session : sessions_)
        session->backend->requestStop();
}

TerminalSessionService::Session* TerminalSessionService::find(core::OperationId id) {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                    [id](const auto& session) { return session->id == id; });
    return found == sessions_.end() ? nullptr : found->get();
}

const TerminalProfile* TerminalSessionService::findProfile(const QString& profileId) const {
    const auto found = std::find_if(
        profiles_.cbegin(), profiles_.cend(),
        [&profileId](const TerminalProfile& profile) { return profile.id == profileId; });
    return found == profiles_.cend() ? nullptr : &*found;
}

void TerminalSessionService::queueEraseSession(core::OperationId id) {
    Session* session = find(id);
    if (!session || session->removalQueued)
        return;
    session->removalQueued = true;
    QTimer::singleShot(0, this, [this, id] { eraseSession(id); });
}

void TerminalSessionService::eraseSession(core::OperationId id) {
    const auto found = std::find_if(sessions_.begin(), sessions_.end(),
                                    [id](const auto& session) { return session->id == id; });
    if (found == sessions_.end())
        return;
    sessions_.erase(found);
    emit sessionRemoved(id);
}

} // namespace litecode::terminal
