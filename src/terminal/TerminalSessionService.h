#pragma once

#include "core/OperationResult.h"
#include "terminal/TerminalTypes.h"

#include <QObject>
#include <QSize>
#include <QVector>

#include <functional>
#include <memory>
#include <vector>

namespace litecode::terminal {

class ITerminalBackend;

struct TerminalSessionSnapshot final {
    core::OperationId id;
    QString profileId;
    QString displayName;
    TerminalState state{TerminalState::Stopped};
};

class TerminalSessionService final : public QObject {
    Q_OBJECT

  public:
    using BackendFactory = std::function<std::unique_ptr<ITerminalBackend>()>;

    explicit TerminalSessionService(QObject* parent = nullptr, BackendFactory factory = {});
    ~TerminalSessionService() override;

    [[nodiscard]] const QVector<TerminalProfile>& profiles() const noexcept;
    [[nodiscard]] QVector<TerminalSessionSnapshot> sessions() const;
    [[nodiscard]] core::OperationId createSession(const QString& profileId,
                                                  const QString& workingDirectory);
    [[nodiscard]] bool sendInput(core::OperationId id, const QByteArray& bytes);
    void resizeSession(core::OperationId id, const QSize& characters);
    void requestRemove(core::OperationId id);
    void requestStopAll();

  signals:
    void sessionCreated(const litecode::terminal::TerminalSessionSnapshot& session);
    void sessionStarted(litecode::core::OperationId id);
    void outputReady(litecode::core::OperationId id, const QByteArray& bytes);
    void foregroundProcessChanged(litecode::core::OperationId id, const QString& processName);
    void sessionStateChanged(litecode::core::OperationId id,
                             litecode::terminal::TerminalState state);
    void sessionCompleted(const litecode::terminal::TerminalExit& result);
    void sessionError(litecode::core::OperationId id, const QString& diagnostic);
    void inputRejected(litecode::core::OperationId id, const QString& diagnostic);
    void sessionRemoved(litecode::core::OperationId id);

  private:
    struct Session;

    [[nodiscard]] Session* find(core::OperationId id);
    [[nodiscard]] const TerminalProfile* findProfile(const QString& profileId) const;
    void queueEraseSession(core::OperationId id);
    void eraseSession(core::OperationId id);

    core::OperationIdGenerator operationIds_;
    QVector<TerminalProfile> profiles_;
    BackendFactory backendFactory_;
    std::vector<std::unique_ptr<Session>> sessions_;
};

} // namespace litecode::terminal

Q_DECLARE_METATYPE(litecode::terminal::TerminalSessionSnapshot)
