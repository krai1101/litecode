#include "ui/RunDebugPanel.h"
#include "core/SettingsService.h"
#include "editor/LanguageSupport.h"
#include "extensions/DebugDescriptorWire.h"
#include "ui/ThemedIcon.h"
#include "ui/components/Controls.h"
#include "ui/debug/DebugInspectorWidget.h"
#include <QApplication>
#include <QComboBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QLabel>
#include <QMetaMethod>
#include <QProcess>
#include <QToolButton>
#include <QUuid>
#include <QVBoxLayout>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace litecode::ui {
RunDebugPanel::RunDebugPanel(QWidget* parent, core::SettingsService* settings)
    : components::SidebarView(parent), session_(this), taskRunner_(this), settings_(settings) {
    setObjectName("runDebugPanel");
    auto* layout = contentLayout();
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    toolbar_ = new QWidget(this);
    toolbar_->setObjectName("runDebugToolbar");
    toolbar_->setFixedHeight(28);
    auto* actions = new QHBoxLayout(toolbar_);
    actions->setContentsMargins(components::SidebarView::toolbarMargins());
    actions->setSpacing(2);
    launchGroup_ = new QWidget(toolbar_);
    launchGroup_->setObjectName("debugLaunchGroup");
    auto* launchLayout = new QHBoxLayout(launchGroup_);
    launchLayout->setContentsMargins(0, 0, 0, 0);
    launchLayout->setSpacing(0);
    debugButton_ = new components::IconButton(launchGroup_);
    debugButton_->setObjectName("startDebuggingButton");
    debugButton_->setIconSize({16, 16});
    debugButton_->setToolTip(tr("Start Debugging (F5)"));
    debugButton_->setProperty("uiIconTone", QStringLiteral("success"));
    setThemedIcon(debugButton_, ":/icons/debug-start.svg");
    launchLayout->addWidget(debugButton_);
    auto* configurations = new components::ComboBox(launchGroup_);
    configurations->setPopupSearch(tr("Search configurations"));
    selector_ = configurations;
    setDebuggerChoices({});
    selector_->setObjectName("debugConfigurationPicker");
    selector_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    selector_->setMinimumWidth(28);
    selector_->setMinimumContentsLength(10);
    selector_->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    launchLayout->addWidget(selector_, 1);
    actions->addWidget(launchGroup_, 1);
    configureButton_ = new components::IconButton(toolbar_);
    configureButton_->setObjectName("debugConfigureButton");
    configureButton_->setIconSize({16, 16});
    configureButton_->setToolTip(tr("Open launch.json"));
    setThemedIcon(configureButton_, ":/icons/settings.svg");
    actions->addWidget(configureButton_);
    sessionControls_ = new QWidget(toolbar_);
    sessionControls_->setObjectName("debugSessionControls");
    auto* sessionLayout = new QHBoxLayout(sessionControls_);
    sessionLayout->setContentsMargins(0, 0, 0, 0);
    sessionLayout->setSpacing(1);
    const auto addControl = [this, sessionLayout](const QString& label, const QString& icon,
                                                  auto handler) {
        auto* button = new components::IconButton(sessionControls_);
        button->setIconSize({16, 16});
        button->setToolTip(label);
        button->setAccessibleName(label);
        setThemedIcon(button, icon);
        sessionLayout->addWidget(button);
        connect(button, &QToolButton::clicked, this, handler);
        return button;
    };
    pauseContinueButton_ = addControl(tr("Pause"), ":/icons/debug-pause.svg", [this] {
        if (isPaused())
            continueDebugging();
        else
            pauseDebugging();
    });
    stepOverButton_ =
        addControl(tr("Step Over"), ":/icons/debug-step-over.svg", &RunDebugPanel::stepOver);
    stepIntoButton_ =
        addControl(tr("Step Into"), ":/icons/debug-step-into.svg", &RunDebugPanel::stepInto);
    stepOutButton_ =
        addControl(tr("Step Out"), ":/icons/debug-step-out.svg", &RunDebugPanel::stepOut);
    stopButton_ = addControl(tr("Stop"), ":/icons/debug-stop.svg", &RunDebugPanel::stop);
    actions->addWidget(sessionControls_, 0, Qt::AlignRight);
    layout->addWidget(toolbar_);
    notice_ = new QLabel(this);
    notice_->setObjectName("runDebugNotice");
    notice_->setWordWrap(true);
    notice_->setContentsMargins(components::SidebarView::controlMargins());
    layout->addWidget(notice_);
    notice_->hide();
    auto* welcome = new QWidget(this);
    welcome_ = welcome;
    welcome_->setObjectName("debugWelcome");
    auto* welcomeLayout = new QVBoxLayout(welcome);
    welcomeLayout->setContentsMargins(4, 0, 8, 0);
    welcomeLayout->setSpacing(0);
    auto* welcomeHeader = new components::IconButton(welcome);
    welcomeHeader->setObjectName("debugSectionHeader");
    welcomeHeader->setText(tr("Run"));
    welcomeHeader->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    welcomeHeader->setIconSize({16, 16});
    welcomeHeader->setAutoRaise(true);
    welcomeHeader->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setThemedIcon(welcomeHeader, ":/icons/chevron-down.svg");
    welcomeLayout->addWidget(welcomeHeader);
    auto* welcomeBody = new QWidget(welcome);
    auto* welcomeBodyLayout = new QVBoxLayout(welcomeBody);
    welcomeBodyLayout->setContentsMargins(16, 14, 12, 0);
    welcomeBodyLayout->setSpacing(12);
    welcomePrompt_ =
        new QLabel(tr("<a href=\"open\">Open a file</a> to run or debug it."), welcomeBody);
    welcomePrompt_->setObjectName("debugWelcomeOpenFile");
    welcomePrompt_->setTextFormat(Qt::RichText);
    welcomePrompt_->setTextInteractionFlags(Qt::LinksAccessibleByMouse |
                                            Qt::LinksAccessibleByKeyboard);
    welcomePrompt_->setWordWrap(true);
    welcomeBodyLayout->addWidget(welcomePrompt_);
    auto* welcomeButton = new components::Button(tr("Run and Debug"), welcomeBody,
                                                 components::ButtonVariant::Primary);
    welcomeButton->setObjectName("runDebugWelcomeButton");
    welcomeButton->setProperty("uiDensity", "compact");
    welcomeButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    welcomeBodyLayout->addWidget(welcomeButton);
    auto* guidance = new QLabel(
        tr("Customize debugging with a <a href=\"launch\">launch.json file</a>."), welcomeBody);
    guidance->setObjectName("debugWelcomeConfiguration");
    guidance->setTextFormat(Qt::RichText);
    guidance->setTextInteractionFlags(Qt::LinksAccessibleByMouse | Qt::LinksAccessibleByKeyboard);
    guidance->setWordWrap(true);
    welcomeBodyLayout->addWidget(guidance);
    welcomeBodyLayout->addStretch();
    welcomeLayout->addWidget(welcomeBody, 1);
    connect(welcomeHeader, &QToolButton::clicked, this,
            [welcomeHeader, welcomeBody, open = true]() mutable {
                open = !open;
                welcomeBody->setVisible(open);
                setThemedIcon(welcomeHeader,
                              open ? ":/icons/chevron-down.svg" : ":/icons/chevron-right.svg");
            });
    layout->addWidget(welcome, 1);
    welcomeDivider_ = new QWidget(this);
    welcomeDivider_->setObjectName("debugWelcomeDivider");
    welcomeDivider_->setFixedHeight(1);
    layout->addWidget(welcomeDivider_);
    inspector_ = new DebugInspectorWidget(session_, this);
    layout->addWidget(inspector_, 1);
    runProcess_ = new QProcess(this);
    connect(debugButton_, &QToolButton::clicked, this, &RunDebugPanel::startDebugging);
    connect(welcomeButton, &QPushButton::clicked, this, &RunDebugPanel::startDebugging);
    connect(welcomePrompt_, &QLabel::linkActivated, this, &RunDebugPanel::openFileRequested);
    connect(guidance, &QLabel::linkActivated, this, &RunDebugPanel::requestConfiguration);
    connect(configureButton_, &QToolButton::clicked, this,
            [this] { emit configurationRequested(); });
    connect(&session_, &debug::DebugSession::errorOccurred, this, [this](const QString& message) {
        showNotice(message);
        appendOutput(message + '\n');
    });
    connect(&session_, &debug::DebugSession::runInTerminalRequested, this,
            &RunDebugPanel::runInTerminalRequested);
    connect(&session_, &debug::DebugSession::outputReceived, this,
            [this](const QJsonObject& event) {
                if (event.value("category").toString() == QStringLiteral("telemetry"))
                    return;
                appendOutput(event.value("output").toString());
            });
    connect(
        &session_, &debug::DebugSession::stateChanged, this,
        [this](debug::DebugSession::State state) {
            emit consoleAvailabilityChanged(state == debug::DebugSession::State::Stopped);
            if (state == debug::DebugSession::State::Running ||
                state == debug::DebugSession::State::Stopped)
                finishDebugStart(true);
            else if (state == debug::DebugSession::State::Idle)
                finishDebugStart(false);
            if (state == debug::DebugSession::State::Initializing && !debugSessionPublished_) {
                debugSessionPublished_ = true;
                emit debugSessionEvent({{"kind", "started"}, {"session", debugSessionContext_}});
                emit debugSessionEvent({{"kind", "active"}, {"session", debugSessionContext_}});
            }
            if ((state == debug::DebugSession::State::Idle ||
                 state == debug::DebugSession::State::Failed) &&
                debugSessionPublished_) {
                debugSessionPublished_ = false;
                emit debugSessionEvent({{"kind", "terminated"}, {"session", debugSessionContext_}});
            }
            if (state == debug::DebugSession::State::Idle ||
                state == debug::DebugSession::State::Failed)
                releaseAdapter();
            notice_->setText(tr("Debug state: %1").arg(static_cast<int>(state)));
            notice_->hide();
            updateControls();
        });
    connect(&session_, &debug::DebugSession::stoppedAt, this, [this](const QJsonObject& details) {
        appendOutput(
            (output_.isEmpty() || output_.endsWith('\n') ? QString() : QStringLiteral("\n")) +
            tr("Paused: %1\n").arg(details.value("reason").toString()));
    });
    connect(&session_, &debug::DebugSession::ended, this, &RunDebugPanel::runPostTask);
    connect(&session_, &debug::DebugSession::customEventReceived, this,
            [this](const QString& event, const QJsonValue& body) {
                if (!debugSessionPublished_)
                    return;
                QJsonObject message{
                    {"kind", "custom"}, {"session", debugSessionContext_}, {"event", event}};
                if (!body.isUndefined())
                    message.insert("body", body);
                emit debugSessionEvent(message);
            });
    connect(&session_, &debug::DebugSession::adapterTrackerEvent, this,
            [this](const QJsonObject& event) { emit debugTrackerEvent(debugSessionId_, event); });
    connect(&session_, &debug::DebugSession::inspectionReady, this,
            [this](int requestId, const QString& command, const QJsonObject& body) {
                if (command != QStringLiteral("evaluate") || !consoleRequests_.remove(requestId))
                    return;
                appendOutput(body.value(QStringLiteral("result")).toString() + '\n');
            });
    connect(inspector_, &DebugInspectorWidget::openLocationRequested, this,
            &RunDebugPanel::openLocationRequested);
    connect(inspector_, &DebugInspectorWidget::activeFrameChanged, this,
            &RunDebugPanel::pausedLocationChanged);
    connect(inspector_, &DebugInspectorWidget::removeBreakpointRequested, this,
            &RunDebugPanel::toggleBreakpoint);
    connect(runProcess_, &QProcess::readyReadStandardOutput, this,
            [this] { appendOutput(QString::fromUtf8(runProcess_->readAllStandardOutput())); });
    connect(runProcess_, &QProcess::readyReadStandardError, this,
            [this] { appendOutput(QString::fromUtf8(runProcess_->readAllStandardError())); });
    connect(runProcess_, &QProcess::errorOccurred, this, [this] {
        appendOutput(runProcess_->errorString());
        if (runProcess_->error() == QProcess::FailedToStart)
            postDebugTask_.clear();
        updateControls();
    });
    connect(runProcess_, &QProcess::finished, this, [this](int code) {
        appendOutput(tr("Process exited with code %1.").arg(code));
        runPostTask();
        updateControls();
    });
    connect(&taskRunner_, &debug::TaskRunner::outputReceived, this, &RunDebugPanel::appendOutput);
    connect(&taskRunner_, &debug::TaskRunner::finished, this,
            [this](bool success, const QString& error) {
                if (!pendingStart_) {
                    if (!success && !error.isEmpty()) {
                        showNotice(error);
                        appendOutput(error + '\n');
                    }
                    updateControls();
                    return;
                }
                pendingStart_ = false;
                if (success) {
                    const auto configuration = pendingConfiguration_;
                    pendingConfiguration_ = {};
                    startPrepared(configuration, pendingDebug_);
                } else {
                    pendingConfiguration_ = {};
                    releaseAdapter();
                    finishDebugStart(false, error);
                    showNotice(error);
                    appendOutput(error + '\n');
                }
                updateControls();
            });
    updateControls();
}
RunDebugPanel::~RunDebugPanel() {
    session_.endAdapterTracking();
    if (debugSessionPublished_) {
        debugSessionPublished_ = false;
        emit debugSessionEvent({{"kind", "terminated"}, {"session", debugSessionContext_}});
    }
    postDebugTask_.clear();
    stop();
    releaseAdapter(); // session member teardown may not emit Idle
}
QWidget* RunDebugPanel::takeTitleActions() {
    contentLayout()->removeWidget(toolbar_);
    toolbar_->setParent(nullptr);
    return toolbar_;
}
bool RunDebugPanel::hasConfigurations() const { return !configurations_.isEmpty(); }
void RunDebugPanel::setActiveFile(const QString& path) {
    activeFile_ = path;
    welcomePrompt_->setVisible(activeFile_.isEmpty() || !supportsBreakpoints(activeFile_));
}
bool RunDebugPanel::isPaused() const {
    return session_.state() == debug::DebugSession::State::Stopped;
}
void RunDebugPanel::setIntegratedTerminalAvailable(bool available) {
    session_.setRunInTerminalEnabled(available);
}
bool RunDebugPanel::respondToRunInTerminal(const QJsonObject& request, bool success,
                                           const QString& error, const QJsonObject& body) {
    const auto accepted = session_.respondToRunInTerminal(request, success, error, body);
    if (success && accepted)
        integratedTerminalActive_ = true;
    return accepted;
}
void RunDebugPanel::setWorkspace(const QString& path) {
    if (workspace_ != path) {
        postDebugTask_.clear();
        stop();
        breakpoints_.clear();
        dynamicConfiguration_.reset();
        inspector_->clearWorkspace();
    }
    workspace_ = path;
    inspector_->setWorkspace(path);
    if (settings_ && !path.isEmpty())
        breakpoints_ = settings_->debugBreakpoints(path);
    refreshBreakpoints();
    const QString selected = selector_->currentText();
    const bool dynamicSelected =
        dynamicConfiguration_ && !configurations_.isEmpty() &&
        selector_->currentIndex() == configurations_.size() - 1 &&
        configurations_.constLast().values == dynamicConfiguration_->values;
    QString error;
    configurations_ = debug::DebugConfigurations::load(path, &error);
    if (dynamicConfiguration_)
        configurations_.append(*dynamicConfiguration_);
    selector_->clear();
    for (const auto& configuration : configurations_)
        selector_->addItem(configuration.name);
    if (configurations_.isEmpty())
        selector_->addItem(tr("No Configurations"));
    const int previous = dynamicSelected ? selector_->count() - 1 : selector_->findText(selected);
    if (previous >= 0)
        selector_->setCurrentIndex(previous);
    showNotice(error);
    updateControls();
}
void RunDebugPanel::setDebuggerSchemas(const QHash<QString, QJsonObject>& schemas) {
    debuggerSchemas_ = schemas;
}
void RunDebugPanel::setDebuggerChoices(const QList<QPair<QString, QString>>& choices) {
    selector_->clearPopupActions();
    for (const auto& [type, label] : choices) {
        selector_->addPopupAction(tr("%1...").arg(label),
                                  [this, type] { emit selectDynamicConfigurationRequested(type); });
    }
    selector_->addPopupAction(tr("Add Configuration..."), [this] { requestConfiguration(); });
}

void RunDebugPanel::selectDynamicConfiguration(const QJsonObject& configuration) {
    if (!canStart())
        return;
    dynamicConfiguration_ = debug::DebugConfiguration{
        configuration.value("name").toString(), configuration.value("type").toString(),
        configuration.value("request").toString(), configuration};
    setWorkspace(workspace_);
    selector_->setCurrentIndex(configurations_.size() - 1);
}
bool RunDebugPanel::prepare(QJsonObject* configuration, bool resolve) {
    // Launch files may be edited while the view is open; resolve the latest saved version.
    setWorkspace(workspace_);
    if (workspace_.isEmpty() || selector_->currentIndex() < 0 ||
        selector_->currentIndex() >= configurations_.size()) {
        showNotice(tr("Create a launch.json configuration to run or debug."));
        return false;
    }
    const auto selected = configurations_.at(selector_->currentIndex());
    if (!resolve) {
        *configuration = selected.values;
        return true;
    }
    const auto attributes = debuggerSchemas_.value(selected.type);
    if (const auto schema = attributes.value(selected.request); schema.isObject()) {
        QString validationError;
        if (!debug::DebugConfigurations::validateAgainstSchema(selected.values, schema.toObject(),
                                                               &validationError)) {
            showNotice(validationError);
            return false;
        }
    }
    auto context = variableContextSource_ ? variableContextSource_() : debug::VariableContext{};
    context.workspaceFolder = workspace_;
    QString error;
    const auto resolved = debug::DebugConfigurations::resolve(selected.values, context, &error);
    if (!error.isEmpty()) {
        showNotice(error);
        return false;
    }
    *configuration = resolved.toObject();
    return true;
}
void RunDebugPanel::startDebugging() {
    if (canStart() && configurations_.isEmpty() &&
        !QFileInfo::exists(debug::DebugConfigurations::filePath(workspace_)) &&
        isSignalConnected(
            QMetaMethod::fromSignal(&RunDebugPanel::selectDebugConfigurationRequested))) {
        emit selectDebugConfigurationRequested();
        return;
    }
    beginStart(true);
}
bool RunDebugPanel::canStart() const {
    return (session_.state() == debug::DebugSession::State::Idle ||
            session_.state() == debug::DebugSession::State::Failed) &&
           runProcess_->state() == QProcess::NotRunning && !taskRunner_.active() && !pendingStart_;
}
void RunDebugPanel::startSelectedConfiguration(const QJsonObject& configuration) {
    if (!canStart())
        return;
    debugStartOptions_ = {};
    launchFolder_ = QJsonValue::Undefined;
    launchVariables_ = variableContextSource_ ? variableContextSource_() : debug::VariableContext{};
    launchVariables_.workspaceFolder = workspace_;
    requestConfigurationStep("resolveDebugConfiguration", configuration);
}
void RunDebugPanel::requestConfiguration() { emit configurationRequested(); }
void RunDebugPanel::runWithoutDebugging() { beginStart(false); }
void RunDebugPanel::beginStart(bool debug) {
    QJsonObject configuration;
    if (!canStart())
        return;
    if (!prepare(&configuration, false))
        return;
    // Contributed debuggers own language launch semantics, including noDebug.
    // Keep the direct executable runner for configurations without a provider.
    if (debug || debuggerSchemas_.contains(configuration.value("type").toString())) {
        if (!debug)
            configuration.insert("noDebug", true);
        startSelectedConfiguration(configuration);
        return;
    }
    if (!prepare(&configuration, true))
        return;
    if (!debug && (configuration.value("request").toString() != "launch" ||
                   configuration.value("program").toString().isEmpty())) {
        showNotice(tr("Run requires a launch configuration with a program."));
        return;
    }
    if (!debug && configuration.contains("args")) {
        if (!configuration.value("args").isArray()) {
            showNotice(tr("Run requires args to be an array of strings."));
            return;
        }
        for (const auto& argument : configuration.value("args").toArray())
            if (!argument.isString()) {
                showNotice(tr("Run requires args to be an array of strings."));
                return;
            }
    }
    if (!debug && configuration.contains("env")) {
        if (!configuration.value("env").isObject()) {
            showNotice(tr("Run requires env to be an object of strings or null."));
            return;
        }
        const auto variables = configuration.value("env").toObject();
        for (auto it = variables.begin(); it != variables.end(); ++it)
            if (!it.value().isString() && !it.value().isNull()) {
                showNotice(tr("Run requires env to be an object of strings or null."));
                return;
            }
    }
    launchVariables_.workspaceFolder = workspace_;
    continuePreparedStart(configuration, debug);
}
void RunDebugPanel::requestConfigurationStep(const QString& method,
                                             const QJsonObject& configuration) {
    pendingConfiguration_ = configuration;
    pendingStart_ = true;
    configurationPhase_ = method;
    pendingConfigurationRequestId_ = ++nextConfigurationRequestId_;
    updateControls();
    if (isSignalConnected(QMetaMethod::fromSignal(&RunDebugPanel::debugConfigurationRequested)))
        emit debugConfigurationRequested(pendingConfigurationRequestId_,
                                         configuration.value("type").toString(), method,
                                         configuration);
    else // A standalone view has no extension providers, but still substitutes variables between
         // phases.
        completeDebugConfigurationRequest(pendingConfigurationRequestId_, configuration, {});
}
void RunDebugPanel::completeDebugConfigurationRequest(quint64 id, const QJsonValue& value,
                                                      const QString& error) {
    if (!pendingStart_ || pendingConfigurationRequestId_ != id)
        return;
    pendingConfigurationRequestId_ = 0;
    pendingConfiguration_ = {};
    if (!error.isEmpty() || !value.isObject()) {
        pendingStart_ = false;
        if (!error.isEmpty())
            showNotice(error);
        else if (!value.isNull() && !value.isUndefined())
            showNotice(tr("Debug configuration provider returned an invalid configuration."));
        if (error.isEmpty() && value.isNull())
            emit configurationRequested();
        finishDebugStart(false, error);
        updateControls();
        return;
    }
    auto configuration = value.toObject();
    if (configurationPhase_ == "resolveDebugConfiguration") {
        QString variableError;
        const auto substituted =
            debug::DebugConfigurations::resolve(configuration, launchVariables_, &variableError);
        if (!variableError.isEmpty()) {
            pendingStart_ = false;
            showNotice(variableError);
            updateControls();
            return;
        }
        requestConfigurationStep("resolveDebugConfigurationWithSubstitutedVariables",
                                 substituted.toObject());
        return;
    }
    pendingStart_ = false;
    const auto type = configuration.value("type").toString();
    const auto request = configuration.value("request").toString();
    if (type.isEmpty() || !configuration.value("name").isString() ||
        (request != "launch" && request != "attach")) {
        showNotice(
            tr("Resolved debug configuration requires name, type and a launch or attach request."));
        updateControls();
        return;
    }
    const auto schema = debuggerSchemas_.value(type).value(request);
    QString validationError;
    if (schema.isObject() && !debug::DebugConfigurations::validateAgainstSchema(
                                 configuration, schema.toObject(), &validationError)) {
        showNotice(validationError);
        updateControls();
        return;
    }
    requestAdapter(configuration);
}
void RunDebugPanel::requestAdapter(const QJsonObject& configuration) {
    debugSessionId_ = QUuid::createUuid().toString(QUuid::WithoutBraces);
    QJsonValue folder = launchFolder_.isUndefined() ? QJsonValue(QJsonValue::Null) : launchFolder_;
    if (launchFolder_.isUndefined() && !workspace_.isEmpty())
        folder = QJsonObject{{"uri", QUrl::fromLocalFile(workspace_).toString(QUrl::FullyEncoded)},
                             {"name", QFileInfo(workspace_).fileName()},
                             {"index", 0}};
    debugSessionContext_ = {{"id", debugSessionId_},
                            {"type", configuration.value("type")},
                            {"name", configuration.value("name")},
                            {"configuration", configuration},
                            {"workspaceFolder", folder}};
    adapterDescriptor_ = {};
    pendingConfiguration_ = configuration;
    pendingDebug_ = true;
    pendingStart_ = true;
    pendingAdapterRequestId_ = ++nextAdapterRequestId_;
    ownedAdapterRequestId_ = pendingAdapterRequestId_;
    emit debugAdapterRequested(pendingAdapterRequestId_, configuration.value("type").toString(),
                               configuration, debugSessionId_);
    updateControls();
}
void RunDebugPanel::completeDebugAdapterRequest(quint64 requestId, const QJsonObject& descriptor,
                                                const QString& error) {
    if (!pendingStart_ || pendingAdapterRequestId_ != requestId)
        return;
    pendingAdapterRequestId_ = 0;
    pendingStart_ = false;
    QJsonObject configuration = std::exchange(pendingConfiguration_, {});
    if (!error.isEmpty() || !extensions::wire::decodeDebugAdapterDescriptor(descriptor)) {
        releaseAdapter();
        showNotice(error.isEmpty() ? tr("Debug adapter factory returned an invalid descriptor.")
                                   : error);
        updateControls();
        return;
    }
    adapterDescriptor_ = descriptor;
    continuePreparedStart(configuration, true);
}
void RunDebugPanel::continuePreparedStart(const QJsonObject& configuration, bool debug) {
    output_.clear();
    integratedTerminalActive_ = false;
    emit outputReset();
    showNotice({});
    const QString task = configuration.value("preLaunchTask").toString();
    if (!task.isEmpty()) {
        pendingConfiguration_ = configuration;
        pendingDebug_ = debug;
        pendingStart_ = true;
        debug::VariableContext context;
        context.workspaceFolder = launchVariables_.workspaceFolder;
        context.environment = QProcessEnvironment::systemEnvironment();
        taskRunner_.start(launchVariables_.workspaceFolder, task, context);
        updateControls();
        return;
    }
    startPrepared(configuration, debug);
}
void RunDebugPanel::startPrepared(const QJsonObject& prepared, bool debug) {
    QJsonObject configuration = prepared;
    postDebugTask_ = configuration.value("postDebugTask").toString();
    launchWorkspace_ = launchVariables_.workspaceFolder;
    if (!debug) {
        const QString program = configuration.value("program").toString();
        const QString cwd = configuration.value("cwd").toString(workspace_);
        if (!QFileInfo(cwd).isDir()) {
            showNotice(tr("Working directory does not exist: %1").arg(cwd));
            return;
        }
        QStringList arguments;
        for (const auto& value : configuration.value("args").toArray())
            arguments.append(value.toString());
        auto environment = QProcessEnvironment::systemEnvironment();
        const auto additions = configuration.value("env").toObject();
        for (auto it = additions.begin(); it != additions.end(); ++it) {
            if (it.value().isNull())
                environment.remove(it.key());
            else
                environment.insert(it.key(), it.value().toString());
        }
        runProcess_->setWorkingDirectory(cwd);
        runProcess_->setProcessEnvironment(environment);
        showNotice({});
        runProcess_->start(program, arguments);
        updateControls();
        return;
    }
    const auto descriptor = extensions::wire::decodeDebugAdapterDescriptor(adapterDescriptor_);
    if (!descriptor) {
        releaseAdapter();
        showNotice(tr("Debug adapter descriptor is unavailable."));
        return;
    }
    showNotice({});
    const auto fileName = QFileInfo(configuration.value("program").toString()).fileName();
    inspector_->setTargetName(fileName.isEmpty() ? configuration.value("name").toString()
                                                 : fileName);
    const QString type = configuration.value("type").toString();
    const QString request = configuration.value("request").toString();
    for (auto it = breakpoints_.cbegin();
         !configuration.value("noDebug").toBool() && it != breakpoints_.cend(); ++it) {
        if (!supportsBreakpoints(it.key()))
            continue;
        QJsonArray lines;
        for (int line : it.value())
            lines.append(QJsonObject{{"line", line}});
        session_.setBreakpoints(it.key(), lines);
    }
    bool started = false;
    if (const auto* executable = std::get_if<extensions::DebugAdapterExecutable>(&*descriptor)) {
        debug::AdapterProcess adapter;
        adapter.executable = executable->command;
        adapter.arguments = executable->args;
        // Adapter cwd/env are distinct from the debug target's launch cwd/env.
        if (executable->options) {
            adapter.workingDirectory = executable->options->cwd.value_or(QString{});
            if (executable->options->env)
                for (auto it = executable->options->env->cbegin();
                     it != executable->options->env->cend(); ++it)
                    adapter.environment.insert(it.key(), it.value());
        }
        started = session_.start(adapter, type, request, configuration);
    } else if (const auto* server = std::get_if<extensions::DebugAdapterServer>(&*descriptor)) {
        started = session_.startServer({static_cast<quint16>(server->port),
                                        server->host.value_or(QStringLiteral("127.0.0.1"))},
                                       type, request, configuration);
    } else if (const auto* pipe =
                   std::get_if<extensions::DebugAdapterNamedPipeServer>(&*descriptor)) {
        started = session_.startNamedPipeServer({pipe->path}, type, request, configuration);
    } else if (const auto* inlineAdapter =
                   std::get_if<extensions::wire::InlineDebugAdapterEndpoint>(&*descriptor)) {
        started =
            session_.startNamedPipeServer({inlineAdapter->path}, type, request, configuration);
    }
    if (!started) {
        releaseAdapter();
        postDebugTask_.clear();
        showNotice(tr("Could not start the debug adapter."));
    }
    updateControls();
}
void RunDebugPanel::runPostTask() {
    const QString task = std::exchange(postDebugTask_, {});
    if (task.isEmpty() || launchWorkspace_.isEmpty())
        return;
    debug::VariableContext context;
    context.workspaceFolder = launchWorkspace_;
    context.environment = QProcessEnvironment::systemEnvironment();
    taskRunner_.start(launchWorkspace_, task, context);
    updateControls();
}
void RunDebugPanel::stop() {
    finishDebugStart(false);
    pendingStart_ = false;
    const auto configurationRequest = std::exchange(pendingConfigurationRequestId_, 0);
    if (configurationRequest)
        emit debugConfigurationCanceled(configurationRequest);
    pendingAdapterRequestId_ = 0;
    pendingConfiguration_ = {};
    taskRunner_.stop();
    if (session_.state() != debug::DebugSession::State::Idle)
        session_.stop();
    else
        releaseAdapter();
    if (runProcess_->state() != QProcess::NotRunning)
        runProcess_->kill();
    updateControls();
}
void RunDebugPanel::cancelOwnedDebugSession(const QString& id) {
    if (id == debugSessionId_)
        stop();
}
void RunDebugPanel::releaseAdapter() {
    const auto id = std::exchange(ownedAdapterRequestId_, 0);
    if (id && !debugSessionPublished_ && !debugSessionContext_.isEmpty())
        emit debugSessionEvent({{"kind", "discarded"}, {"session", debugSessionContext_}});
    if (id)
        emit debugAdapterReleased(id);
}
void RunDebugPanel::continueDebugging() { session_.continueExecution(session_.stoppedThread()); }
void RunDebugPanel::pauseDebugging() {
    session_.pause(session_.stoppedThread() > 0 ? session_.stoppedThread() : 1);
}
void RunDebugPanel::stepOver() { session_.stepOver(session_.stoppedThread()); }
void RunDebugPanel::stepInto() { session_.stepInto(session_.stoppedThread()); }
void RunDebugPanel::stepOut() { session_.stepOut(session_.stoppedThread()); }
QFuture<QJsonValue> RunDebugPanel::handleDebugSessionRequest(const QString& id,
                                                             const QString& operation,
                                                             const QString& command,
                                                             const QJsonValue& arguments) {
    QPromise<QJsonValue> promise;
    promise.start();
    auto result = promise.future();
    if (!debugSessionPublished_ || id != debugSessionId_) {
        promise.setException(
            std::make_exception_ptr(std::runtime_error("Debug session is not available.")));
        promise.finish();
        return result;
    }
    if (operation == "customRequest")
        return session_.customRequest(command, arguments);
    if (operation == "stop")
        return session_.stopAsync();
    if (operation == "setName" && arguments.isString() && !arguments.toString().isEmpty()) {
        debugSessionContext_.insert("name", arguments);
        if (selector_->currentIndex() >= 0)
            selector_->setItemText(selector_->currentIndex(), arguments.toString());
        inspector_->setAccessibleName(arguments.toString());
        emit debugSessionEvent({{"kind", "changed"}, {"session", debugSessionContext_}});
        promise.addResult(QJsonValue::Undefined);
    } else
        promise.setException(
            std::make_exception_ptr(std::runtime_error("Unsupported debug session operation.")));
    promise.finish();
    return result;
}
void RunDebugPanel::evaluateConsole(const QString& expression) {
    if (expression.trimmed().isEmpty())
        return;
    if (session_.state() != debug::DebugSession::State::Running &&
        session_.state() != debug::DebugSession::State::Stopped) {
        appendOutput(tr("Start a debug session to evaluate expressions.\n"));
        return;
    }
    appendOutput(QStringLiteral("> ") + expression + '\n');
    const int request =
        session_.evaluate(expression, QStringLiteral("repl"), inspector_->selectedFrame());
    if (request > 0)
        consoleRequests_.insert(request);
}
QString RunDebugPanel::outputText() const { return output_; }
void RunDebugPanel::showNotice(const QString& text) {
    if (!text.isEmpty())
        finishDebugStart(false, text);
    notice_->setText(text);
    notice_->setVisible(!text.isEmpty());
}
void RunDebugPanel::appendOutput(const QString& text) {
    output_ += text;
    constexpr qsizetype maxOutputCharacters = 256 * 1024;
    if (output_.size() > maxOutputCharacters)
        output_ = output_.right(maxOutputCharacters);
    emit outputReceived(text);
}
void RunDebugPanel::toggleBreakpoint(const QString& path, int line) {
    if (!QFileInfo(path).isAbsolute() || line < 1 ||
        (!supportsBreakpoints(path) && !hasBreakpoint(path, line)))
        return;
    auto& lines = breakpoints_[QDir::cleanPath(path)];
    if (lines.contains(line))
        lines.remove(line);
    else
        lines.insert(line);
    if (lines.isEmpty())
        breakpoints_.remove(QDir::cleanPath(path));
    refreshBreakpoints();
    if (settings_)
        settings_->saveDebugBreakpoints(workspace_, breakpoints_);
    QJsonArray points;
    for (int current : breakpoints_.value(QDir::cleanPath(path)))
        points.append(QJsonObject{{"line", current}});
    session_.setBreakpoints(QDir::cleanPath(path), points);
}
bool RunDebugPanel::hasBreakpoint(const QString& path, int line) const {
    return breakpoints_.value(QDir::cleanPath(path)).contains(line);
}
bool RunDebugPanel::supportsBreakpoints(const QString& path) const {
    return !breakpointLanguages_ ||
           breakpointLanguages_->contains(editor::languageSupportForFile(path).id);
}
void RunDebugPanel::setBreakpointLanguages(const QSet<QString>& languages) {
    breakpointLanguages_ = languages;
    setActiveFile(activeFile_);
    refreshBreakpoints();
}
void RunDebugPanel::refreshBreakpoints() {
    auto visible = breakpoints_;
    for (auto it = visible.begin(); it != visible.end();)
        if (!supportsBreakpoints(it.key()))
            it = visible.erase(it);
        else
            ++it;
    inspector_->setBreakpoints(visible);
    emit breakpointsChanged(visible);
}
void RunDebugPanel::updateControls() {
    // The workbench may destroy the reparented title actions before this panel.
    if (!toolbar_)
        return;
    const auto state = session_.state();
    const bool active =
        state != debug::DebugSession::State::Idle && state != debug::DebugSession::State::Failed;
    const bool running = runProcess_->state() != QProcess::NotRunning;
    const bool startingTask = taskRunner_.active() || pendingStart_;
    const bool available = !configurations_.isEmpty() && !workspace_.isEmpty();
    const bool launchControls = !active && !running && !startingTask;
    const bool showWelcome = launchControls && configurations_.isEmpty();
    welcome_->setVisible(showWelcome);
    welcomeDivider_->setVisible(showWelcome);
    inspector_->setInspectionVisible(!showWelcome);
    const bool toolbarAvailable = launchControls
                                      ? !configurations_.isEmpty()
                                      : !debugStartOptions_.value("suppressDebugToolbar").toBool();
    if (toolbar_->property("sidebarTitleActionsAvailable") != QVariant(toolbarAvailable)) {
        toolbar_->setProperty("sidebarTitleActionsAvailable", toolbarAvailable);
        if (toolbar_->parentWidget() == this)
            toolbar_->setVisible(toolbarAvailable);
        emit titleActionsAvailabilityChanged(toolbarAvailable);
    }
    launchGroup_->setVisible(launchControls);
    configureButton_->setVisible(launchControls);
    toolbar_->layout()->setContentsMargins(
        launchControls ? components::SidebarView::toolbarMargins() : QMargins{});
    debugButton_->setEnabled(available && launchControls);
    sessionControls_->setVisible(!launchControls);

    const bool paused = isPaused();
    const QString executionAction = paused ? tr("Continue (F5)") : tr("Pause");
    pauseContinueButton_->setToolTip(executionAction);
    pauseContinueButton_->setAccessibleName(executionAction);
    setThemedIcon(pauseContinueButton_,
                  paused ? ":/icons/debug-continue.svg" : ":/icons/debug-pause.svg");
    pauseContinueButton_->setEnabled(paused || state == debug::DebugSession::State::Running);
    for (auto* button : {stepOverButton_, stepIntoButton_, stepOutButton_})
        button->setEnabled(paused);
    stopButton_->setEnabled(state != debug::DebugSession::State::Terminating);
}
} // namespace litecode::ui
