#include "debug/DebugSession.h"
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <cmath>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>

namespace litecode::debug {
DebugSession::DebugSession(QObject* parent) : QObject(parent), transport_(this) {
    handshake_.setSingleShot(true);
    connect(&handshake_, &QTimer::timeout, this,
            [this] { fail("Debug session initialization timed out."); });
    connect(&transport_, &DapTransport::started, this, &DebugSession::initialize);
    connect(&transport_, &DapTransport::responseReceived, this,
            [this](int id, const QString&, bool success, const QJsonObject& body,
                   const QString& message) {
                auto reply = replies_.take(id);
                if (reply)
                    reply(success, body, message);
            });
    connect(&transport_, &DapTransport::eventReceived, this, &DebugSession::handleEvent);
    connect(
        &transport_, &DapTransport::responseValueReceived, this,
        [this](int id, const QString&, bool success, const QJsonValue& body, const QString& error) {
            auto promise = customRequests_.take(id);
            if (!promise)
                return;
            if (success)
                promise->addResult(body);
            else
                promise->setException(std::make_exception_ptr(std::runtime_error(
                    (error.isEmpty() ? QStringLiteral("Debug adapter rejected the request.")
                                     : error)
                        .toStdString())));
            promise->finish();
        });
    connect(&transport_, &DapTransport::eventValueReceived, this,
            [this](const QString& event, const QJsonValue& body) {
                static const QSet<QString> standard{
                    "initialized",   "stopped",        "continued",   "exited",
                    "terminated",    "thread",         "output",      "breakpoint",
                    "module",        "loadedSource",   "process",     "capabilities",
                    "progressStart", "progressUpdate", "progressEnd", "invalidated",
                    "memory"};
                if (!standard.contains(event) && state_ != State::Idle && state_ != State::Failed)
                    emit customEventReceived(event, body);
            });
    connect(&transport_, &DapTransport::errorOccurred, this, [this](const QString& error) {
        if (trackerStarted_)
            emit adapterTrackerEvent({{"kind", "error"}, {"error", error}});
        fail(error);
    });
    connect(&transport_, &DapTransport::messageSent, this, [this](const QJsonObject& message) {
        if (trackerStarted_)
            emit adapterTrackerEvent({{"kind", "receive"}, {"message", message}});
    });
    connect(&transport_, &DapTransport::messageReceived, this, [this](const QJsonObject& message) {
        if (trackerStarted_)
            emit adapterTrackerEvent({{"kind", "send"}, {"message", message}});
    });
    connect(&transport_, &DapTransport::diagnosticOutput, this, [this](const QByteArray& bytes) {
        emit outputReceived({{"category", "stderr"}, {"output", QString::fromUtf8(bytes)}});
    });
    connect(&transport_, &DapTransport::reverseRequestReceived, this,
            [this](const QJsonObject& request) {
                if (request.value("command").toString() == QStringLiteral("runInTerminal") &&
                    runInTerminalEnabled_) {
                    const auto sequence = request.value("seq").toInt();
                    if (terminalRequests_.contains(sequence)) {
                        fail("Duplicate runInTerminal request.");
                        return;
                    }
                    if (terminalRequests_.size() >= 64 ||
                        nextTerminalRequestId_ == std::numeric_limits<quint64>::max()) {
                        transport_.respond(request, false, {},
                                           "Too many pending debug terminal requests.");
                        return;
                    }
                    const auto token = QString::number(++nextTerminalRequestId_);
                    terminalRequests_.insert(sequence, token);
                    QJsonObject forwarded = request;
                    forwarded.insert("_litecodeTerminalRequest", token);
                    emit runInTerminalRequested(forwarded);
                    return;
                }
                transport_.respond(request, false, {},
                                   "Unsupported reverse request: " +
                                       request.value("command").toString());
            });
    connect(&transport_, &DapTransport::stateChanged, this, [this](DapTransport::State state) {
        if (state != DapTransport::State::Stopping)
            return;
        if (trackerStarted_ && !trackerStopping_) {
            trackerStopping_ = true;
            emit adapterTrackerEvent({{"kind", "stop"}});
        }
        replies_.clear();
        terminalRequests_.clear();
        rejectCustomRequests("Debug session closed.");
        invalidateReferences();
        if (state_ != State::Failed)
            setState(State::Terminating);
    });
    connect(&transport_, &DapTransport::stopped, this, [this](int code, bool crashed) {
        if (crashed && trackerStarted_)
            emit adapterTrackerEvent(
                {{"kind", "error"},
                 {"error", "Debug adapter crashed (exit " + QString::number(code) + ")."}});
        finishAdapterTracking(processAdapter_ ? std::optional<int>(code) : std::nullopt);
        handshake_.stop();
        replies_.clear();
        rejectCustomRequests("Debug session closed.");
        if (crashed && state_ != State::Failed) {
            setState(State::Failed);
            emit errorOccurred("Debug adapter crashed (exit " + QString::number(code) + ").");
        } else if (state_ != State::Failed)
            setState(State::Idle);
        const auto stops = std::exchange(stopRequests_, {});
        for (const auto& promise : stops) {
            promise->addResult(QJsonValue::Undefined);
            promise->finish();
        }
        emit ended();
    });
}
DebugSession::~DebugSession() {
    endAdapterTracking();
    rejectCustomRequests("Debug session was disposed.");
    const auto stops = std::exchange(stopRequests_, {});
    for (const auto& promise : stops) {
        promise->setException(
            std::make_exception_ptr(std::runtime_error("Debug session was disposed.")));
        promise->finish();
    }
}
void DebugSession::endAdapterTracking() { finishAdapterTracking({}); }
void DebugSession::finishAdapterTracking(std::optional<int> exitCode) {
    if (!trackerStarted_)
        return;
    if (!trackerStopping_) {
        trackerStopping_ = true;
        emit adapterTrackerEvent({{"kind", "stop"}});
    }
    trackerStarted_ = false;
    QJsonObject event{{"kind", "exit"}};
    if (exitCode)
        event.insert("code", *exitCode);
    emit adapterTrackerEvent(event);
}
QFuture<QJsonValue> DebugSession::stopAsync() {
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    auto result = promise->future();
    if (transport_.state() == DapTransport::State::Idle) {
        promise->addResult(QJsonValue::Undefined);
        promise->finish();
        return result;
    }
    stopRequests_.append(promise);
    stop();
    return result;
}
void DebugSession::rejectCustomRequests(const QString& error) {
    const auto pending = std::exchange(customRequests_, {});
    for (const auto& promise : pending) {
        promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
        promise->finish();
    }
}
QFuture<QJsonValue> DebugSession::customRequest(const QString& command,
                                                const QJsonValue& arguments) {
    auto promise = std::make_shared<QPromise<QJsonValue>>();
    promise->start();
    auto future = promise->future();
    if (command.isEmpty() || command.contains(QChar('\0')) || state_ == State::Idle ||
        state_ == State::Failed || state_ == State::Terminating) {
        promise->setException(
            std::make_exception_ptr(std::runtime_error("Debug session is not available.")));
        promise->finish();
        return future;
    }
    const auto id = transport_.requestValue(command, arguments);
    if (id && transport_.state() == DapTransport::State::Running)
        customRequests_.insert(id, promise);
    else {
        promise->setException(
            std::make_exception_ptr(std::runtime_error("Debug adapter is not available.")));
        promise->finish();
    }
    return future;
}
bool DebugSession::respondToRunInTerminal(const QJsonObject& request, bool success,
                                          const QString& error, const QJsonObject& body) {
    const auto sequence = request.value("seq").toInt();
    const auto token = request.value("_litecodeTerminalRequest").toString();
    if (request.value("command").toString() != QStringLiteral("runInTerminal") || token.isEmpty() ||
        terminalRequests_.value(sequence) != token)
        return false;
    terminalRequests_.remove(sequence);
    return transport_.respond(request, success, body, error);
}
void DebugSession::setState(State state) {
    if (state_ == state)
        return;
    state_ = state;
    emit stateChanged(state);
}
void DebugSession::invalidateReferences() {
    ++inspectionEpoch_;
    resuming_ = false;
    stoppedThread_ = 0;
    emit referencesInvalidated();
}
bool DebugSession::supports(const QString& capability) const {
    return capabilities_.value(capability).toBool();
}
bool DebugSession::start(const AdapterProcess& adapter, const QString& adapterId,
                         const QString& request, const QJsonObject& arguments) {
    return startWithTransport([this, &adapter] { return transport_.start(adapter); }, adapterId,
                              request, arguments, true);
}
bool DebugSession::startServer(const AdapterServer& adapter, const QString& adapterId,
                               const QString& request, const QJsonObject& arguments) {
    return startWithTransport([this, &adapter] { return transport_.startServer(adapter); },
                              adapterId, request, arguments, false);
}
bool DebugSession::startNamedPipeServer(const AdapterNamedPipeServer& adapter,
                                        const QString& adapterId, const QString& request,
                                        const QJsonObject& arguments) {
    return startWithTransport([this, &adapter] { return transport_.startNamedPipeServer(adapter); },
                              adapterId, request, arguments, false);
}
bool DebugSession::startWithTransport(const std::function<bool()>& startTransport,
                                      const QString& adapterId, const QString& request,
                                      const QJsonObject& arguments, bool processAdapter) {
    if (transport_.state() != DapTransport::State::Idle ||
        (state_ != State::Idle && state_ != State::Failed) || adapterId.isEmpty() ||
        (request != "launch" && request != "attach"))
        return false;
    replies_.clear();
    terminalRequests_.clear();
    capabilities_ = {};
    exceptionFilters_.clear();
    adapterId_ = adapterId;
    launchRequest_ = request;
    arguments_ = arguments;
    initialized_ = launchSent_ = configurationStarted_ = false;
    debuggeeExited_ = false;
    launchCompleted_ = configurationCompleted_ = false;
    configuring_ = 0;
    invalidateReferences();
    setState(State::Starting);
    if (state_ != State::Starting)
        return false;
    processAdapter_ = processAdapter;
    trackerStarted_ = true;
    trackerStopping_ = false;
    emit adapterTrackerEvent({{"kind", "start"}});
    if (state_ != State::Starting) {
        finishAdapterTracking({});
        return false;
    }
    handshake_.start(30000);
    if (!startTransport()) {
        handshake_.stop();
        finishAdapterTracking({});
        setState(State::Failed);
        return false;
    }
    return true;
}
int DebugSession::send(const QString& command, const QJsonObject& arguments, Reply callback,
                       int timeoutMs) {
    const auto id = transport_.request(command, arguments, timeoutMs);
    if (id)
        replies_.insert(id, std::move(callback));
    else if (callback)
        callback(false, {}, "Debug adapter is not available.");
    return id;
}
void DebugSession::initialize() {
    if (state_ != State::Starting)
        return;
    setState(State::Initializing);
    send(
        "initialize",
        {{"clientID", "litecode"},
         {"clientName", "LiteCode"},
         {"adapterID", adapterId_},
         {"pathFormat", "path"},
         {"linesStartAt1", true},
         {"columnsStartAt1", true},
         {"locale", QLocale().name()},
         {"supportsRunInTerminalRequest", runInTerminalEnabled_},
         {"supportsVariablePaging", true}},
        [this](bool success, const QJsonObject& body, const QString& message) {
            if (!success) {
                fail("Debug initialization failed: " + message);
                return;
            }
            capabilities_ = body;
            emit capabilitiesChanged(body);
            if (state_ != State::Initializing)
                return;
            setState(State::Configuring);
            if (state_ != State::Configuring)
                return;
            launchSent_ = true;
            send(
                launchRequest_, arguments_,
                [this](bool ok, const QJsonObject&, const QString& error) {
                    if (!ok) {
                        fail("Debug " + launchRequest_ + " failed: " + error);
                        return;
                    }
                    launchCompleted_ = true;
                    readyToRun();
                },
                30000);
            configure();
        },
        30000);
}
void DebugSession::configure() {
    if (!initialized_ || !launchSent_ || configurationStarted_ || state_ != State::Configuring)
        return;
    configurationStarted_ = true;
    if (arguments_.value("noDebug").toBool()) {
        configurationFinished();
        return;
    }
    const auto sources = breakpoints_;
    const bool supportsExceptionFilters =
        !capabilities_.value("exceptionBreakpointFilters").toArray().isEmpty();
    configuring_ = static_cast<int>(sources.size()) + (supportsExceptionFilters ? 1 : 0);
    if (!configuring_) {
        configurationFinished();
        return;
    }
    if (supportsExceptionFilters) {
        QJsonArray filters;
        for (const auto& filter : exceptionFilters_)
            filters.append(filter);
        send("setExceptionBreakpoints", {{"filters", filters}},
             [this](bool success, const QJsonObject&, const QString& message) {
                 if (!success) {
                     fail("Could not configure exception breakpoints: " + message);
                     return;
                 }
                 if (--configuring_ == 0)
                     configurationFinished();
             });
    }
    for (auto it = sources.cbegin(); it != sources.cend(); ++it) {
        const auto path = it.key();
        send("setBreakpoints",
             {{"source", QJsonObject{{"path", QDir::toNativeSeparators(path)},
                                     {"name", QFileInfo(path).fileName()}}},
              {"breakpoints", it.value()}},
             [this, path](bool success, const QJsonObject& body, const QString& message) {
                 if (!success) {
                     fail("Could not configure breakpoints: " + message);
                     return;
                 }
                 emit breakpointsUpdated(path, body.value("breakpoints").toArray());
                 if (--configuring_ == 0)
                     configurationFinished();
             });
        if (state_ == State::Failed || state_ == State::Terminating)
            return;
    }
}
void DebugSession::configurationFinished() {
    if (state_ != State::Configuring)
        return;
    if (supports("supportsConfigurationDoneRequest"))
        send("configurationDone", {},
             [this](bool success, const QJsonObject&, const QString& message) {
                 if (!success)
                     fail("Debug configuration failed: " + message);
                 else {
                     configurationCompleted_ = true;
                     readyToRun();
                 }
             });
    else {
        configurationCompleted_ = true;
        readyToRun();
    }
}
void DebugSession::readyToRun() {
    if (!launchCompleted_ || !configurationCompleted_)
        return;
    handshake_.stop();
    if (state_ == State::Configuring) {
        setState(State::Running);
        threads();
    }
}
bool DebugSession::setBreakpoints(const QString& path, const QJsonArray& breakpoints) {
    if (!QFileInfo(path).isAbsolute() || path.contains(QChar('\0')))
        return false;
    for (const auto& value : breakpoints) {
        if (!value.isObject())
            return false;
        const auto point = value.toObject();
        const auto line = point.value("line").toDouble();
        if (!point.value("line").isDouble() || line < 1 || line > 2147483647 ||
            std::floor(line) != line)
            return false;
        for (const auto& key : {"condition", "hitCondition", "logMessage"})
            if (point.contains(key) && !point.value(key).isString())
                return false;
        if (launchSent_ &&
            ((point.contains("condition") && !supports("supportsConditionalBreakpoints")) ||
             (point.contains("hitCondition") && !supports("supportsHitConditionalBreakpoints")) ||
             (point.contains("logMessage") && !supports("supportsLogPoints")))) {
            emit errorOccurred(
                "This adapter does not support the requested breakpoint condition or logpoint.");
            return false;
        }
    }
    breakpoints_.insert(path, breakpoints);
    if (!arguments_.value("noDebug").toBool() &&
        (state_ == State::Running || state_ == State::Stopped)) {
        return send("setBreakpoints",
                    {{"source", QJsonObject{{"path", QDir::toNativeSeparators(path)}}},
                     {"breakpoints", breakpoints}},
                    [this, path](bool success, const QJsonObject& body, const QString& error) {
                        if (success)
                            emit breakpointsUpdated(path, body.value("breakpoints").toArray());
                        else
                            emit errorOccurred(error);
                    }) > 0;
    }
    return true;
}
bool DebugSession::setExceptionBreakpoints(const QStringList& filters) {
    for (const auto& filter : filters)
        if (filter.isEmpty() || filter.contains(QChar('\0')))
            return false;
    exceptionFilters_ = filters;
    if (!arguments_.value("noDebug").toBool() &&
        (state_ == State::Running || state_ == State::Stopped)) {
        QJsonArray enabled;
        for (const auto& filter : filters)
            enabled.append(filter);
        return send("setExceptionBreakpoints", {{"filters", enabled}},
                    [this](bool success, const QJsonObject&, const QString& message) {
                        if (!success)
                            emit errorOccurred("Could not configure exception breakpoints: " +
                                               message);
                    }) > 0;
    }
    return true;
}
void DebugSession::handleEvent(const QString& name, const QJsonObject& body) {
    if (state_ == State::Idle || state_ == State::Failed || state_ == State::Terminating)
        return;
    if (name == "initialized") {
        initialized_ = true;
        configure();
    } else if (name == "output")
        emit outputReceived(body);
    else if (name == "capabilities") {
        const auto changes = body.value("capabilities").toObject();
        for (auto it = changes.begin(); it != changes.end(); ++it)
            capabilities_.insert(it.key(), it.value());
        emit capabilitiesChanged(capabilities_);
    } else if (name == "stopped") {
        handshake_.stop();
        invalidateReferences();
        stoppedThread_ = body.value("threadId").toInt();
        setState(State::Stopped);
        emit stoppedAt(body);
        threads();
        if (stoppedThread_ > 0)
            stackTrace(stoppedThread_);
    } else if (name == "continued") {
        invalidateReferences();
        setState(State::Running);
    } else if (name == "breakpoint")
        emit breakpointEvent(body);
    else if (name == "thread") {
        if (body.value("reason") == "started")
            threads();
        else if (body.value("reason") == "exited" &&
                 body.value("threadId").toInt() == stoppedThread_) {
            // Thread-exit notifications already describe the removal. Asking
            // the adapter for threads here races the debuggee's shutdown.
            invalidateReferences();
            if (state_ == State::Stopped)
                setState(State::Running);
        }
    } else if (name == "terminated")
        stop();
    else if (name == "exited") {
        debuggeeExited_ = true;
        invalidateReferences();
        emit outputReceived(
            {{"category", "console"},
             {"output", "Process exited with code " +
                            QString::number(body.value("exitCode").toInt()) + ".\n"}});
    }
}
void DebugSession::fail(const QString& error) {
    if (state_ == State::Failed)
        return;
    handshake_.stop();
    setState(State::Failed);
    emit errorOccurred(error);
    transport_.stop();
}
bool DebugSession::control(const QString& command, int threadId) {
    if (state_ != State::Stopped || resuming_ || threadId <= 0)
        return false;
    // An adapter can resume the program before acknowledging the request.
    // Stop issuing suspended-frame requests and drop late inspection replies.
    const auto epoch = ++inspectionEpoch_;
    resuming_ = true;
    const auto id = send(command, {{"threadId", threadId}},
                         [this, epoch](bool success, const QJsonObject&, const QString& error) {
                             if (epoch != inspectionEpoch_)
                                 return;
                             resuming_ = false;
                             if (!success) {
                                 emit errorOccurred(error);
                                 return;
                             }
                             invalidateReferences();
                             setState(State::Running);
                         });
    if (!id)
        resuming_ = false;
    return id > 0;
}
bool DebugSession::continueExecution(int threadId) { return control("continue", threadId); }
bool DebugSession::stepOver(int threadId) { return control("next", threadId); }
bool DebugSession::stepInto(int threadId) { return control("stepIn", threadId); }
bool DebugSession::stepOut(int threadId) { return control("stepOut", threadId); }
bool DebugSession::pause(int threadId) {
    if (state_ != State::Running || threadId <= 0)
        return false;
    return send("pause", {{"threadId", threadId}},
                [this](bool success, const QJsonObject&, const QString& error) {
                    if (!success)
                        emit errorOccurred(error);
                }) > 0;
}
void DebugSession::stop() {
    handshake_.stop();
    if (state_ == State::Starting || state_ == State::Initializing) {
        setState(State::Terminating);
        transport_.stop();
        if (transport_.state() == DapTransport::State::Idle)
            setState(State::Idle);
        return;
    }
    if (state_ == State::Idle || state_ == State::Failed || state_ == State::Terminating)
        return;
    setState(State::Terminating);
    invalidateReferences();
    // Never terminate an attached process when detaching.
    send(
        "disconnect", {{"terminateDebuggee", launchRequest_ == "launch"}, {"restart", false}},
        [this](bool success, const QJsonObject&, const QString& error) {
            if (success)
                transport_.stopAfterDisconnect();
            else {
                emit errorOccurred("Debug disconnect failed: " + error);
                transport_.stop();
            }
        },
        3000);
}
int DebugSession::inspect(const QString& command, const QJsonObject& arguments,
                          bool suspendedOnly) {
    if (debuggeeExited_)
        return 0;
    if (suspendedOnly ? state_ != State::Stopped || resuming_
                      : (state_ != State::Running && state_ != State::Stopped))
        return 0;
    const auto epoch = inspectionEpoch_;
    // Request IDs are assigned before an asynchronous process can respond.
    auto id = std::make_shared<int>(0);
    *id = send(
        command, arguments,
        [this, epoch, command, id](bool success, const QJsonObject& body, const QString& error) {
            if (epoch != inspectionEpoch_)
                return;
            if (success)
                emit inspectionReady(*id, command, body);
            else
                emit errorOccurred(error);
        });
    return *id;
}
int DebugSession::threads() { return inspect("threads", {}, false); }
int DebugSession::stackTrace(int threadId, int startFrame, int levels) {
    if (threadId <= 0 || startFrame < 0 || levels <= 0)
        return 0;
    return inspect("stackTrace",
                   {{"threadId", threadId}, {"startFrame", startFrame}, {"levels", levels}}, true);
}
int DebugSession::scopes(int frameId) {
    if (frameId < 0)
        return 0;
    return inspect("scopes", {{"frameId", frameId}}, true);
}
int DebugSession::variables(int reference, int start, int count) {
    if (reference <= 0 || start < 0 || count <= 0)
        return 0;
    return inspect("variables",
                   {{"variablesReference", reference}, {"start", start}, {"count", count}}, true);
}
int DebugSession::evaluate(const QString& expression, const QString& context, int frameId) {
    if (expression.isEmpty() || (context != "watch" && context != "repl" && context != "hover"))
        return 0;
    if (context == "hover" && !supports("supportsEvaluateForHovers"))
        return 0;
    QJsonObject arguments{{"expression", expression}, {"context", context}};
    if (frameId >= 0)
        arguments.insert("frameId", frameId);
    return inspect("evaluate", arguments, context != "repl" || frameId >= 0);
}
} // namespace litecode::debug
