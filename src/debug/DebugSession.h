#pragma once
#include "debug/DapTransport.h"
#include <QJsonArray>
#include <QMap>
#include <QStringList>
#include <QFuture>
#include <QPromise>
#include <optional>
#include <functional>

namespace litecode::debug {
class DebugSession final : public QObject {
    Q_OBJECT
  public:
    enum class State {
        Idle,
        Starting,
        Initializing,
        Configuring,
        Running,
        Stopped,
        Terminating,
        Failed
    };
    Q_ENUM(State)
    explicit DebugSession(QObject* parent = nullptr);
    ~DebugSession() override;
    State state() const { return state_; }
    QJsonObject capabilities() const { return capabilities_; }
    int stoppedThread() const { return stoppedThread_; }
    void setRunInTerminalEnabled(bool enabled) { runInTerminalEnabled_ = enabled; }
    bool respondToRunInTerminal(const QJsonObject& request, bool success,
                                const QString& error = {}, const QJsonObject& body = {});
    bool start(const AdapterProcess& adapter, const QString& adapterId, const QString& request,
               const QJsonObject& arguments);
    bool startServer(const AdapterServer& adapter, const QString& adapterId, const QString& request,
                     const QJsonObject& arguments);
    bool startNamedPipeServer(const AdapterNamedPipeServer& adapter, const QString& adapterId,
                              const QString& request, const QJsonObject& arguments);
    bool setBreakpoints(const QString& path, const QJsonArray& breakpoints);
    bool setExceptionBreakpoints(const QStringList& filters);
    bool continueExecution(int threadId);
    bool pause(int threadId);
    bool stepOver(int threadId);
    bool stepInto(int threadId);
    bool stepOut(int threadId);
    void stop();
    QFuture<QJsonValue> stopAsync();
    void endAdapterTracking();
    int threads();
    int stackTrace(int threadId, int startFrame = 0, int levels = 50);
    int scopes(int frameId);
    int variables(int reference, int start = 0, int count = 100);
    int evaluate(const QString& expression, const QString& context, int frameId = -1);
    QFuture<QJsonValue> customRequest(const QString& command, const QJsonValue& arguments = QJsonValue::Undefined);
  signals:
    void stateChanged(litecode::debug::DebugSession::State state);
    void capabilitiesChanged(const QJsonObject& capabilities);
    void outputReceived(const QJsonObject& output);
    void stoppedAt(const QJsonObject& details);
    void referencesInvalidated();
    void breakpointsUpdated(const QString& path, const QJsonArray& breakpoints);
    void breakpointEvent(const QJsonObject& details);
    void inspectionReady(int requestId, const QString& command, const QJsonObject& body);
    void errorOccurred(const QString& message);
    void runInTerminalRequested(const QJsonObject& request);
    void ended();
    void customEventReceived(const QString& event, const QJsonValue& body);
    void adapterTrackerEvent(const QJsonObject& event);

  private:
    using Reply = std::function<void(bool, const QJsonObject&, const QString&)>;
    bool startWithTransport(const std::function<bool()>& startTransport, const QString& adapterId,
                            const QString& request, const QJsonObject& arguments, bool processAdapter);
    int send(const QString& command, const QJsonObject& arguments, Reply callback,
             int timeoutMs = 10000);
    int inspect(const QString& command, const QJsonObject& arguments, bool suspendedOnly);
    void initialize();
    void configure();
    void configurationFinished();
    void readyToRun();
    void handleEvent(const QString& name, const QJsonObject& body);
    void setState(State state);
    void invalidateReferences();
    void fail(const QString& error);
    bool control(const QString& command, int threadId);
    bool supports(const QString& capability) const;
    void rejectCustomRequests(const QString& error);
    void finishAdapterTracking(std::optional<int> exitCode);
    DapTransport transport_;
    QHash<int, Reply> replies_;
    QHash<int, std::shared_ptr<QPromise<QJsonValue>>> customRequests_;
    QVector<std::shared_ptr<QPromise<QJsonValue>>> stopRequests_;
    QMap<QString, QJsonArray> breakpoints_;
    QStringList exceptionFilters_;
    QJsonObject capabilities_, arguments_;
    QString adapterId_, launchRequest_;
    State state_{State::Idle};
    quint64 inspectionEpoch_{};
    int stoppedThread_{};
    int configuring_{};
    bool initialized_{};
    bool debuggeeExited_{};
    bool launchSent_{};
    bool configurationStarted_{};
    bool launchCompleted_{};
    bool configurationCompleted_{};
    bool runInTerminalEnabled_{};
    bool trackerStarted_{}, trackerStopping_{}, processAdapter_{};
    QHash<int, QString> terminalRequests_;
    quint64 nextTerminalRequestId_{};
    QTimer handshake_;
};
} // namespace litecode::debug
Q_DECLARE_METATYPE(litecode::debug::DebugSession::State)
