#include "extensions/ExtensionManager.h"
#include "extensions/ChatRequestWire.h"
#include "extensions/ConfigurationRequests.h"
#include "extensions/ConfigurationService.h"
#include "extensions/ConfigurationWire.h"
#include "extensions/DebugAdapterContribution.h"
#include "extensions/DebugDescriptorWire.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/JsonMessageTransfer.h"
#include "extensions/NativeExtensionUninstaller.h"
#include "extensions/TextEditorEditWire.h"
#include "extensions/UriWire.h"
#include "extensions/WorkspaceEditTransfer.h"
#include "extensions/WorkspaceEditWire.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QLibrary>
#include <QLocalServer>
#include <QLocalSocket>
#include <QPluginLoader>
#include <QPointer>
#include <QProcess>
#include <QScopeGuard>
#include <QSet>
#include <QSettings>
#include <QTimer>
#include <QUuid>
#include <QtConcurrent>
#include <algorithm>
#include <stdexcept>

namespace litecode::extensions {
struct ExtensionManager::Entry {
    QObject callbackLifetime;
    ExtensionInfo info;
    QJsonObject manifest;
    std::unique_ptr<QProcess> process;
    std::unique_ptr<QLocalServer> listener;
    QLocalSocket* socket{};
    wire::Decoder decoder;
    wire::WorkspaceEditTransferDecoder workspaceEditTransfer;
    wire::JsonMessageTransferDecoder editorEditTransfer{
        {"event", "editorApplyEdit", "editorApplyEdit"}};
    wire::JsonMessageTransferDecoder clipboardTransfer{{"event", "clipboard", "clipboard"}};
    QSet<QString> runtimeCommands;
    QSet<QString> runtimeFormatters;
    QSet<QString> runtimeDebugAdapterFactories;
    QSet<QString> runtimeChatParticipants;
    QHash<QString, QJsonValue> runtimeChatIcons;
    QHash<QString, bool> runtimeChatFollowups;
    struct CommandRequest {
        quint64 id;
        QString command;
        QJsonArray arguments;
    };
    QVector<CommandRequest> pendingCommands;
    QSet<quint64> inFlightCommands;
    QSet<quint64> clientCommandRequestIds;
    struct FormatRequest {
        quint64 id;
        QString language;
        QString text;
    };
    QVector<FormatRequest> pendingFormats;
    QSet<quint64> inFlightFormats;
    struct DebugRequest {
        quint64 id;
        QString type;
        QJsonObject session;
    };
    QVector<DebugRequest> pendingDebugRequests;
    QSet<quint64> inFlightDebugRequests;
    QSet<quint64> activeInlineAdapters;
    QHash<quint64, QJsonObject> debugRequestContexts;
    struct ChatRequest {
        quint64 id{};
        QString sessionId;
        QString participantId;
        QJsonObject request;
        QJsonArray contextHistory;
        QJsonArray parts;
    };
    QVector<ChatRequest> pendingChats;
    QHash<quint64, ChatRequest> inFlightChats;
    QString token;
    QByteArray trustedHash;
    bool transitioning{};
    bool authenticated{};
    bool stopping{};
};
static QByteArray libraryHash(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&file))
        return {};
    return hash.result().toHex();
}
static QString duplicateContributedCommand(const QJsonObject& candidate, const QJsonObject& other) {
    QSet<QString> ids;
    for (const auto& value : candidate.value("contributes").toObject().value("commands").toArray())
        ids.insert(value.toObject().value("command").toString());
    for (const auto& value : other.value("contributes").toObject().value("commands").toArray()) {
        const auto id = value.toObject().value("command").toString();
        if (ids.contains(id))
            return id;
    }
    return {};
}
static QString duplicateContributedParticipant(const QJsonObject& candidate,
                                               const QJsonObject& other) {
    QSet<QString> ids;
    for (const auto& value :
         candidate.value("contributes").toObject().value("chatParticipants").toArray())
        ids.insert(value.toObject().value("id").toString());
    for (const auto& value :
         other.value("contributes").toObject().value("chatParticipants").toArray()) {
        const auto id = value.toObject().value("id").toString();
        if (ids.contains(id))
            return id;
    }
    return {};
}
static QSet<QString> contributedNativeViewIds(const QJsonObject& manifest) {
    QSet<QString> ids;
    const auto views = manifest.value("contributes").toObject().value("views").toObject();
    for (auto it = views.begin(); it != views.end(); ++it)
        for (const auto& value : it.value().toArray())
            ids.insert(value.toObject().value("id").toString());
    return ids;
}
ExtensionManager::ExtensionManager(const QString& directory, QObject* parent)
    : QObject(parent), directory_(directory) {
    connect(this, &ExtensionManager::changed, this,
            [this] { nativeViews_.setContributions(nativeViewContributions()); });
    connect(&nativeViews_, &WorkbenchNativeViews::activationRequested, this,
            [this](const QString& owner) {
                for (const auto& entry : entries_)
                    if (entry->info.id == owner && entry->info.enabled) {
                        if (!entry->info.error.isEmpty() || !activate(*entry))
                            nativeViews_.removeOwner(owner, entry->info.error);
                        return;
                    }
            });
    connect(&nativeViews_, &WorkbenchNativeViews::sendRequested, this,
            [this](const QString& owner, const QJsonObject& message) {
                for (const auto& entry : entries_)
                    if (entry->info.id == owner && entry->authenticated && !entry->stopping) {
                        sendHostMessage(*entry, message);
                        return;
                    }
            });
    debugSessions_ = std::make_unique<WorkbenchDebugSessions>();
    debugStarts_ = std::make_unique<WorkbenchDebugStarts>();
    debugTrackers_ = std::make_unique<WorkbenchDebugTrackers>(
        [this](const QString& owner, const QJsonObject& message) {
            for (const auto& entry : entries_) {
                if (entry->info.id != owner || !entry->authenticated || entry->stopping)
                    continue;
                if (message.value("command") == QJsonValue("debugTrackerEvent")) {
                    bool exhausted{};
                    wire::sendJsonTransfer(
                        message, {"command", "debugTrackerEvent", "debugTrackerEvent"},
                        [this, target = entry.get(), &exhausted](const QJsonObject& part) {
                            if (exhausted)
                                return;
                            constexpr qint64 MaxQueuedBytes = 32 * 1024 * 1024;
                            if (target->socket &&
                                target->socket->bytesToWrite() > MaxQueuedBytes - 65536) {
                                exhausted = true;
                                target->info.error =
                                    "Debug adapter observation queue exceeded its size limit.";
                                target->socket->abort();
                                if (target->process)
                                    target->process->kill();
                                return;
                            }
                            sendHostMessage(*target, part);
                        });
                } else
                    sendHostMessage(*entry, message);
                return;
            }
            debugTrackers_->removeOwner(owner);
        });
    connect(debugTrackers_.get(), &WorkbenchDebugTrackers::prepared, this,
            &ExtensionManager::debugAdapterResult);
    connect(debugTrackers_.get(), &WorkbenchDebugTrackers::adapterOwnerUnavailable, this,
            &ExtensionManager::debugSessionOwnerUnavailable);
    debugConfigurations_ = std::make_unique<WorkbenchDebugConfigurations>(
        [this](const QString& owner, const QJsonObject& message) {
            for (const auto& entry : entries_) {
                if (entry->info.id == owner && entry->authenticated && !entry->stopping) {
                    sendHostMessage(*entry, message);
                    return;
                }
            }
            debugConfigurations_->removeOwner(owner,
                                              "Debug configuration extension host is unavailable.");
        });
    connect(debugConfigurations_.get(), &WorkbenchDebugConfigurations::finished, this,
            &ExtensionManager::debugConfigurationResult);
    environment_ = std::make_unique<WorkbenchEnvironment>(
        QDir(directory_).absoluteFilePath("state/environment.json"));
    contentProviders_ = std::make_unique<WorkbenchContentProviders>();
    connect(contentProviders_.get(), &WorkbenchContentProviders::contentRequested, this,
            [this](const QString& owner, quint64 handle, quint64 request, const QUrl& uri) {
                for (const auto& entry : entries_) {
                    if (entry->info.id != owner || !entry->authenticated || entry->stopping)
                        continue;
                    sendHostMessage(*entry, {{"command", "provideTextDocumentContent"},
                                             {"handle", QString::number(handle)},
                                             {"requestId", QString::number(request)},
                                             {"uri", uri.toString(QUrl::FullyEncoded)}});
                    return;
                }
                contentProviders_->fail(
                    request, QStringLiteral("Content provider extension host is unavailable."));
            });
    // One profile-wide file for all native extensions, not per-extension
    // settings silos. Keeping it under this manager's state root also isolates
    // alternate profiles and independently constructed managers.
    configurationService_ = std::make_unique<ConfigurationService>(
        QDir(directory_).absoluteFilePath("state/User/settings.json"));
    connect(configurationService_.get(), &ConfigurationService::changed, this, [this] {
        const auto snapshot = wire::encodeConfigurationModel(configurationService_->model());
        for (const auto& entry : entries_)
            if (entry->authenticated && !entry->stopping)
                sendHostMessage(*entry, {{"command", "configuration"}, {"model", snapshot}});
    });
    connect(this, &ExtensionManager::commandResult, this,
            [this](quint64 requestId, const QJsonValue& result, const QString& error) {
                const auto forwarded = forwardedCommandRequests_.take(requestId);
                if (!forwarded.source ||
                    !forwarded.source->clientCommandRequestIds.remove(forwarded.sourceRequestId))
                    return;
                QJsonObject reply{{"command", "extensionCommandResult"},
                                  {"requestId", QString::number(forwarded.sourceRequestId)},
                                  {"error", error}};
                if (error.isEmpty() && !result.isUndefined())
                    reply.insert("result", result);
                sendHostMessage(*forwarded.source, reply);
            });
    documents_ = std::make_unique<WorkbenchDocuments>([this](const QJsonObject& message) {
        for (const auto& entry : entries_)
            if (entry->authenticated && !entry->stopping)
                sendHostMessage(*entry, message);
    });
    outputs_ = std::make_unique<WorkbenchOutput>();
    messages_ = std::make_unique<WorkbenchMessages>();
    dialogs_ = std::make_unique<WorkbenchDialogs>();
    inputBox_ = std::make_unique<WorkbenchInputBox>();
    quickPick_ = std::make_unique<WorkbenchQuickPick>();
    quickInputCoordinator_ =
        std::make_unique<WorkbenchQuickInputCoordinator>(*inputBox_, *quickPick_);
    statusBar_ = std::make_unique<WorkbenchStatusBar>();
    externalUris_ = std::make_unique<WorkbenchExternalUris>();
    uriHandlers_ = std::make_unique<WorkbenchUriHandlers>();
    connect(uriHandlers_.get(), &WorkbenchUriHandlers::error, this, &ExtensionManager::uriError);
    connect(uriHandlers_.get(), &WorkbenchUriHandlers::dispatched, this,
            &ExtensionManager::uriDispatched);
    connect(uriHandlers_.get(), &WorkbenchUriHandlers::activationRequested, this,
            [this](const QString& owner) {
                for (auto& entry : entries_) {
                    if (entry->info.id.compare(owner, Qt::CaseInsensitive) != 0 ||
                        !entry->info.enabled || entry->stopping)
                        continue;
                    if ((!entry->process || entry->process->state() == QProcess::NotRunning) &&
                        !entry->manifest.value("activationEvents").toArray().contains("onUri")) {
                        uriHandlers_->removeOwner(owner);
                        emit uriError(owner, "The extension has no onUri activation event.");
                        return;
                    }
                    if (!activate(*entry)) {
                        uriHandlers_->removeOwner(owner);
                        emit uriError(owner, "Could not activate URI handler extension.");
                    }
                    return;
                }
            });
    connect(statusBar_.get(), &WorkbenchStatusBar::commandRequested, this,
            [this](const QString& command, const QJsonArray& arguments) {
                const auto request = requestCommand(command, arguments);
                if (request)
                    statusBarCommandRequests_.insert(request);
                else
                    emit statusBar_->error("Command is unavailable: " + command);
            });
    connect(this, &ExtensionManager::commandResult, statusBar_.get(),
            [this](quint64 request, const QJsonValue&, const QString& error) {
                if (statusBarCommandRequests_.remove(request) && !error.isEmpty())
                    emit statusBar_->error(error);
            });
    progress_ = std::make_unique<WorkbenchProgress>();
    connect(outputs_.get(), &WorkbenchOutput::globalLogLevelChanged, this, [this](int value) {
        environment_->setLogLevel(static_cast<LogLevel>(value));
        for (const auto& entry : entries_)
            if (entry->authenticated && !entry->stopping)
                sendHostMessage(*entry, {{"command", "environmentLogLevel"}, {"level", value}});
    });
    connect(progress_.get(), &WorkbenchProgress::cancelRequested, this,
            [this](const QString& owner, quint64 id) {
                for (const auto& entry : entries_) {
                    if (entry->info.id != owner || !entry->authenticated || entry->stopping)
                        continue;
                    sendHostMessage(*entry,
                                    {{"command", "progressCancel"}, {"id", QString::number(id)}});
                    break;
                }
            });
    connect(outputs_.get(), &WorkbenchOutput::logLevelRequested, this,
            [this](const QString& owner, const QString& id, int level) {
                for (const auto& entry : entries_) {
                    if (entry->info.id != owner || !entry->authenticated || entry->stopping)
                        continue;
                    sendHostMessage(*entry,
                                    {{"command", "outputLevel"}, {"id", id}, {"level", level}});
                    break;
                }
            });
    connect(outputs_.get(), &WorkbenchOutput::clearRequested, this,
            [this](const QString& owner, const QString& id) {
                for (const auto& entry : entries_) {
                    if (entry->info.id != owner || !entry->authenticated || entry->stopping)
                        continue;
                    sendHostMessage(*entry, {{"command", "outputClear"}, {"id", id}});
                    break;
                }
            });
}
ExtensionManager::~ExtensionManager() {
    for (auto& entry : entries_)
        deactivate(*entry);
    debugStarts_.reset();
    debugConfigurations_.reset();
    debugSessions_.reset();
    debugTrackers_.reset();
    // QObject would otherwise delete a still-running retired QProcess and log
    // a warning. Shutdown may wait here; interactive disable never does.
    for (auto* process : findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly)) {
        if (process->state() == QProcess::NotRunning)
            continue;
        process->kill();
        process->waitForFinished(1000);
    }
}
void ExtensionManager::setHostExecutablePath(const QString& path) { hostExecutablePath_ = path; }
void ExtensionManager::setWindowState(WindowState state) {
    if (windowState_ == state)
        return;
    windowState_ = state;
    for (const auto& entry : entries_) {
        if (entry->authenticated && entry->process && !entry->stopping)
            sendHostMessage(
                *entry, {{"command", "windowState"}, {"state", wire::encodeWindowState(state)}});
    }
}
void ExtensionManager::setEditorStateSource(
    std::function<WindowEditorState()> source,
    std::function<bool(const QString&, const TextEditorEditData&)> applyEdit) {
    editorStateSource_ = std::move(source);
    editorEdit_ = std::move(applyEdit);
    if (editorStateSource_)
        setEditorState(editorStateSource_());
}
void ExtensionManager::setEditorState(const WindowEditorState& state) {
    if (wire::sameEditorState(editorState_, state))
        return;
    editorState_ = state;
    for (const auto& entry : entries_)
        if (entry->authenticated && entry->process && !entry->stopping)
            sendHostMessage(
                *entry, {{"command", "editorState"}, {"state", wire::encodeEditorState(state)}});
}
void ExtensionManager::setDefaultShell(const QString& path) {
    if (path.contains(QChar('\0')) || path.size() > 65536)
        return;
    defaultShell_ = path;
    if (!environment_->setShell(path))
        return;
    for (const auto& entry : entries_)
        if (entry->authenticated && !entry->stopping)
            sendHostMessage(*entry, {{"command", "environmentShell"}, {"shell", path}});
}
void ExtensionManager::setWorkbenchCommandSource(std::function<QStringList()> ids,
                                                 std::function<bool(const QString&)> invoke) {
    workbenchCommandIds_ = std::move(ids);
    invokeWorkbenchCommand_ = std::move(invoke);
}
void ExtensionManager::discover() {
    // Discovery reads metadata only; it never loads an unapproved native library.
    QSettings settings(QDir(directory_).filePath("enabled.ini"), QSettings::IniFormat);
    QSet<QString> ids;
    QSet<QString> knownPaths;
    for (const auto& entry : entries_) {
        ids.insert(entry->info.id);
        knownPaths.insert(entry->info.path);
    }
    // Legacy loose libraries and one root per installed package. Resources and
    // bundled runtime plugins are not independent LiteCode extensions.
    const QDir root(directory_);
    auto libraries = root.entryInfoList(QDir::Files | QDir::NoSymLinks);
    for (const auto& package :
         root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks))
        libraries.append(
            QDir(package.absoluteFilePath()).entryInfoList(QDir::Files | QDir::NoSymLinks));
    for (const auto& file : libraries) {
        if (knownPaths.contains(file.absoluteFilePath()))
            continue;
        if (!QLibrary::isLibrary(file.fileName()) || file.isSymLink())
            continue;
        auto entry = std::make_unique<Entry>();
        // Reading Qt metadata does not load the plugin into LiteCode.
        QPluginLoader metadataLoader(file.absoluteFilePath());
        const auto metadata = metadataLoader.metaData();
        if (!metadata.value("IID").toString().startsWith("org.litecode.Extension/"))
            continue;
        entry->manifest = metadata.value("MetaData").toObject();
        auto& info = entry->info;
        info.path = file.absoluteFilePath();
        info.id = entry->manifest.value("id").toString();
        info.name = entry->manifest.value("displayName").toString();
        info.version = entry->manifest.value("version").toString();
        info.description = entry->manifest.value("description").toString();
        info.error = validateManifest(entry->manifest);
        const auto icon = QFileInfo(file.dir().filePath(entry->manifest.value("icon").toString()))
                              .canonicalFilePath();
        const auto packageRoot = file.dir().canonicalPath();
        const auto relativeIcon = QDir(packageRoot).relativeFilePath(icon);
        if (!icon.isEmpty() && QFileInfo(icon).isFile() && !QDir::isAbsolutePath(relativeIcon) &&
            relativeIcon != ".." && !relativeIcon.startsWith("../"))
            info.iconPath = icon;
        if (ids.contains(info.id)) {
            info.error = "Duplicate extension ID.";
            for (auto& previous : entries_)
                if (previous->info.id == info.id) {
                    previous->info.error = info.error;
                    previous->info.enabled = false;
                }
        }
        ids.insert(info.id);
        entry->trustedHash = settings.value(info.id + "/trustedHash").toByteArray();
        info.enabled =
            info.error.isEmpty() && settings.value(info.id + "/enabled", false).toBool() &&
            !entry->trustedHash.isEmpty() && entry->trustedHash == libraryHash(info.path);
        entries_.push_back(std::move(entry));
    }
    QHash<QString, Entry*> contributedCommandOwners;
    QHash<QString, Entry*> contributedParticipantOwners;
    const auto workbenchIds = workbenchCommandIds_ ? workbenchCommandIds_() : QStringList{};
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("commands").toArray()) {
            const auto id = value.toObject().value("command").toString();
            if (workbenchIds.contains(id)) {
                entry->info.error = "Duplicate contributed command ID: " + id;
                entry->info.enabled = false;
                break;
            }
            if (auto* owner = contributedCommandOwners.value(id)) {
                owner->info.error = "Duplicate contributed command ID: " + id;
                owner->info.enabled = false;
                entry->info.error = owner->info.error;
                entry->info.enabled = false;
            } else if (entry->info.enabled) {
                contributedCommandOwners.insert(id, entry.get());
            }
        }
        if (!entry->info.enabled)
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("chatParticipants").toArray()) {
            const auto id = value.toObject().value("id").toString();
            if (auto* owner = contributedParticipantOwners.value(id)) {
                owner->info.error = "Duplicate contributed chat participant ID: " + id;
                owner->info.enabled = false;
                entry->info.error = owner->info.error;
                entry->info.enabled = false;
            } else if (entry->info.enabled) {
                contributedParticipantOwners.insert(id, entry.get());
            }
        }
    }
    QHash<QString, Entry*> nativeViewOwners;
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& id : contributedNativeViewIds(entry->manifest)) {
            if (auto* owner = nativeViewOwners.value(id)) {
                owner->info.error = entry->info.error =
                    "Duplicate contributed native view ID: " + id;
                owner->info.enabled = entry->info.enabled = false;
            } else
                nativeViewOwners.insert(id, entry.get());
        }
    }
    rebuildConfigurationContributions();
    for (auto& entry : entries_) {
        if (!entry->info.enabled)
            continue;
        if (entry->manifest.value("activationEvents").toArray().contains("onStartupFinished"))
            activate(*entry);
    }
    emit changed();
}
QVector<ExtensionInfo> ExtensionManager::extensions() const {
    QVector<ExtensionInfo> result;
    for (const auto& entry : entries_)
        result.append(entry->info);
    return result;
}
QVector<NativeViewContribution> ExtensionManager::nativeViewContributions() const {
    QVector<NativeViewContribution> result;
    QSet<QString> ids;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !validateManifest(entry->manifest).isEmpty() ||
            entry->info.error.startsWith("Duplicate "))
            continue;
        const auto c = entry->manifest.value("contributes").toObject();
        const auto containers = c.value("viewsContainers").toObject();
        const auto views = c.value("views").toObject();
        for (auto it = containers.begin(); it != containers.end(); ++it)
            for (const auto& value : it.value().toArray()) {
                const auto container = value.toObject();
                const auto containerId = container.value("id").toString();
                for (const auto& item : views.value(containerId).toArray()) {
                    const auto view = item.toObject();
                    const auto id = view.value("id").toString();
                    if (ids.contains(id))
                        continue;
                    ids.insert(id);
                    result.append({id, view.value("name").toString(), containerId,
                                   container.value("title").toString(), it.key(),
                                   container.value("icon").toString(), entry->info.id,
                                   QFileInfo(entry->info.path).absolutePath()});
                }
            }
    }
    return result;
}
bool ExtensionManager::activate(Entry& entry) {
    if (entry.info.uninstalling)
        return false;
    if (entry.info.active)
        return true;
    if (entry.process) {
        // Activation is asynchronous. Requests arriving while this host starts
        // must queue for `ready`; only a stopped/failed host needs re-enabling.
        return entry.process->state() != QProcess::NotRunning && entry.info.error.isEmpty();
    }
    if (entry.transitioning)
        return false;
    if (!environment_->error().isEmpty()) {
        environment_ = std::make_unique<WorkbenchEnvironment>(
            QDir(directory_).absoluteFilePath("state/environment.json"));
        environment_->setShell(defaultShell_);
        environment_->setLogLevel(outputs_->globalLogLevel());
        if (!environment_->error().isEmpty()) {
            entry.info.error = environment_->error();
            return false;
        }
    }
    entry.transitioning = true;
    auto finishTransition = qScopeGuard([&entry] { entry.transitioning = false; });
    if (entry.trustedHash.isEmpty() || libraryHash(entry.info.path) != entry.trustedHash) {
        entry.info.error = "Extension binary changed. Restart and approve the new binary.";
        return false;
    }
    const QString executable = hostExecutablePath_.isEmpty()
                                   ? QDir(QCoreApplication::applicationDirPath())
                                         .filePath(
#ifdef Q_OS_WIN
                                             "LiteCodeExtensionHost.exe")
#else
                                             "LiteCodeExtensionHost")
#endif
                                   : hostExecutablePath_;
    if (!QFile::exists(executable)) {
        entry.info.error = "LiteCode extension host was not found: " + executable;
        return false;
    }
    entry.listener = std::make_unique<QLocalServer>();
    entry.listener->setSocketOptions(QLocalServer::UserAccessOption);
    const auto socketName = "lc-host-" + QUuid::createUuid().toString(QUuid::Id128);
    if (!entry.listener->listen(socketName)) {
        entry.info.error =
            "Could not start extension host listener: " + entry.listener->errorString();
        entry.listener.reset();
        return false;
    }
    entry.token = QUuid::createUuid().toString(QUuid::WithoutBraces);
    entry.authenticated = false;
    entry.stopping = false;
    entry.process = std::make_unique<QProcess>();
    // IPC uses the local socket, so plugin stdout/stderr cannot corrupt it or
    // accumulate unbounded data in QProcess buffers.
    entry.process->setProcessChannelMode(QProcess::ForwardedChannels);
    auto* extension = &entry;
    const QPointer<QProcess> processGuard(entry.process.get());
    // Queued signals and the activation deadline can outlive disable/re-enable.
    // They belong to this process, never to a replacement stored in the same Entry.
    connect(
        entry.listener.get(), &QLocalServer::newConnection, this, [this, extension, processGuard] {
            if (!processGuard || extension->process.get() != processGuard.data() ||
                !extension->listener)
                return;
            auto& current = *extension;
            auto* incoming = current.listener->nextPendingConnection();
            if (current.socket) {
                incoming->disconnectFromServer();
                incoming->deleteLater();
                return;
            }
            current.socket = incoming;
            connect(incoming, &QLocalSocket::readyRead, this, [this, extension, processGuard] {
                if (!processGuard || extension->process.get() != processGuard.data())
                    return;
                auto& current = *extension;
                while (processGuard && current.process.get() == processGuard.data() &&
                       current.socket && current.socket->bytesAvailable() > 0) {
                    const auto result = current.decoder.consume(current.socket->read(64 * 1024));
                    if (!result.error.isEmpty()) {
                        current.info.error = result.error;
                        current.process->kill();
                        return;
                    }
                    for (const auto& message : result.messages) {
                        if (!processGuard || current.process.get() != processGuard.data() ||
                            current.stopping)
                            return;
                        handleHostMessage(current, message);
                    }
                }
            });
        });
    connect(entry.process.get(), &QProcess::errorOccurred, this,
            [this, extension, processGuard](QProcess::ProcessError error) {
                if (!processGuard || extension->process.get() != processGuard.data() ||
                    extension->stopping)
                    return;
                if (extension->info.error.isEmpty())
                    extension->info.error =
                        "Extension host failed: " + extension->process->errorString();
                if (error == QProcess::FailedToStart) {
                    failCommandRequests(*extension, extension->info.error);
                    for (const auto& request : extension->pendingFormats)
                        emit documentFormatResult(request.id, {}, extension->info.error);
                    extension->pendingFormats.clear();
                    failDebugRequests(*extension, extension->info.error);
                    failChatRequests(*extension, extension->info.error);
                    extension->listener->close();
                }
                emit changed();
            });
    connect(entry.process.get(), &QProcess::finished, this,
            [this, extension, processGuard](int exitCode, QProcess::ExitStatus status) {
                if (!processGuard || extension->process.get() != processGuard.data() ||
                    extension->stopping)
                    return;
                if (extension->info.error.isEmpty() ||
                    (status == QProcess::CrashExit &&
                     extension->info.error ==
                         "Extension host failed: " + extension->process->errorString())) {
                    extension->info.error =
                        status == QProcess::CrashExit
                            ? QStringLiteral("Extension host crashed (exit 0x%1).")
                                  .arg(QString::number(static_cast<quint32>(exitCode), 16)
                                           .rightJustified(8, '0'))
                            : "Extension host exited (code " + QString::number(exitCode) + ").";
                }
                discardForwardedCommandRequests(*extension);
                extension->info.active = false;
                nativeViews_.removeOwner(extension->info.id, extension->info.error);
                outputs_->removeOwner(extension->info.id);
                messages_->removeOwner(extension->info.id);
                dialogs_->removeOwner(extension->info.id);
                inputBox_->removeOwner(extension->info.id);
                quickPick_->removeOwner(extension->info.id);
                statusBar_->removeOwner(extension->info.id);
                externalUris_->removeOwner(extension->info.id);
                uriHandlers_->removeOwner(extension->info.id);
                progress_->removeOwner(extension->info.id);
                documents_->removeOwner(extension->info.id);
                contentProviders_->removeOwner(extension->info.id);
                extension->runtimeCommands.clear();
                failCommandRequests(
                    *extension,
                    extension->info.error.isEmpty()
                        ? QStringLiteral("Extension host exited before command completed.")
                        : extension->info.error);
                extension->runtimeFormatters.clear();
                extension->runtimeDebugAdapterFactories.clear();
                const auto formatError =
                    extension->info.error.isEmpty()
                        ? QStringLiteral("Extension host exited before formatting completed.")
                        : extension->info.error;
                for (const auto& request : extension->pendingFormats)
                    emit documentFormatResult(request.id, {}, formatError);
                extension->pendingFormats.clear();
                for (quint64 id : std::as_const(extension->inFlightFormats))
                    emit documentFormatResult(id, {}, formatError);
                extension->inFlightFormats.clear();
                failDebugRequests(*extension, formatError);
                extension->runtimeChatParticipants.clear();
                extension->runtimeChatIcons.clear();
                extension->runtimeChatFollowups.clear();
                failChatRequests(*extension, formatError);
                extension->listener->close();
                extension->decoder.reset();
                extension->workspaceEditTransfer.reset();
                extension->editorEditTransfer.reset();
                extension->clipboardTransfer.reset();
                extension->authenticated = false;
                emit changed();
            });
    entry.process->start(executable,
                         {"--socket", socketName, "--token", entry.token, "--plugin",
                          entry.info.path, "--hash", QString::fromLatin1(entry.trustedHash), "--id",
                          entry.info.id, "--workspace", workspace_, "--storage-root",
                          QDir(directory_).absoluteFilePath("state")});
    QTimer::singleShot(10000, &entry.callbackLifetime, [this, extension, processGuard] {
        if (!processGuard || extension->process.get() != processGuard.data() ||
            extension->stopping || extension->info.active ||
            extension->process->state() == QProcess::NotRunning)
            return;
        extension->info.error = "Extension host activation timed out.";
        extension->process->kill();
        emit changed();
    });
    return true;
}
void ExtensionManager::deactivate(Entry& entry) {
    if (entry.transitioning)
        return;
    entry.transitioning = true;
    auto finishTransition = qScopeGuard([&entry] { entry.transitioning = false; });
    entry.stopping = true;
    nativeViews_.removeOwner(entry.info.id);
    outputs_->removeOwner(entry.info.id);
    messages_->removeOwner(entry.info.id);
    dialogs_->removeOwner(entry.info.id);
    inputBox_->removeOwner(entry.info.id);
    quickPick_->removeOwner(entry.info.id);
    statusBar_->removeOwner(entry.info.id);
    externalUris_->removeOwner(entry.info.id);
    uriHandlers_->removeOwner(entry.info.id);
    progress_->removeOwner(entry.info.id);
    documents_->removeOwner(entry.info.id);
    contentProviders_->removeOwner(entry.info.id);
    discardForwardedCommandRequests(entry);
    if (entry.process) {
        auto* process = entry.process.release();
        process->setParent(this);
        process->setProperty("extensionLibrary", entry.info.path);
        disconnect(process, nullptr, this, nullptr);
        sendHostMessage(entry, {{"command", "shutdown"}});
        if (entry.socket) {
            // Keep the connection alive long enough for the graceful shutdown
            // message to reach the host after its listener is removed.
            entry.socket->flush();
            entry.socket->setParent(process);
            disconnect(entry.socket, nullptr, this, nullptr);
        }
        if (process->state() == QProcess::NotRunning) {
            process->deleteLater();
        } else {
            connect(process, &QProcess::finished, process, &QObject::deleteLater);
            // Match upstream's five-second extension shutdown grace period.
            // A blocked native hook cannot block the workbench: escalation is
            // owned by its separate process and remains bounded.
            QTimer::singleShot(5000, process, [process] {
                if (process->state() == QProcess::NotRunning)
                    return;
                process->terminate();
                QTimer::singleShot(500, process, [process] {
                    if (process->state() != QProcess::NotRunning)
                        process->kill();
                });
            });
        }
    }
    if (entry.listener)
        disconnect(entry.listener.get(), nullptr, this, nullptr);
    entry.listener.reset();
    entry.socket = nullptr;
    entry.decoder.reset();
    entry.workspaceEditTransfer.reset();
    entry.editorEditTransfer.reset();
    entry.clipboardTransfer.reset();
    entry.runtimeCommands.clear();
    failCommandRequests(entry, "Extension disabled.");
    entry.runtimeFormatters.clear();
    entry.runtimeDebugAdapterFactories.clear();
    for (const auto& request : entry.pendingFormats)
        emit documentFormatResult(request.id, {}, "Extension disabled.");
    entry.pendingFormats.clear();
    for (quint64 id : std::as_const(entry.inFlightFormats))
        emit documentFormatResult(id, {}, "Extension disabled.");
    entry.inFlightFormats.clear();
    failDebugRequests(entry, "Extension disabled.");
    entry.runtimeChatParticipants.clear();
    entry.runtimeChatIcons.clear();
    entry.runtimeChatFollowups.clear();
    failChatRequests(entry, "Extension disabled.");
    entry.authenticated = false;
    entry.info.active = false;
    entry.stopping = false;
}
void ExtensionManager::sendHostMessage(Entry& entry, const QJsonObject& message) {
    if (entry.socket && entry.authenticated &&
        entry.socket->state() == QLocalSocket::ConnectedState) {
        const auto frame = wire::encode(message);
        if (frame.isEmpty()) {
            entry.info.error = "Extension host request exceeded size limit.";
            if (entry.process)
                entry.process->kill();
            return;
        }
        entry.socket->write(frame);
    }
}
void ExtensionManager::failDebugRequests(Entry& entry, const QString& error) {
    debugConfigurations_->removeOwner(entry.info.id, error);
    debugSessions_->removeOwner(entry.info.id);
    debugStarts_->removeOwner(entry.info.id);
    debugTrackers_->removeOwner(entry.info.id);
    entry.debugRequestContexts.clear();
    const auto pending = std::exchange(entry.pendingDebugRequests, {});
    const auto inFlight = std::exchange(entry.inFlightDebugRequests, {});
    entry.activeInlineAdapters.clear();
    for (const auto& request : pending)
        emit debugAdapterResult(request.id, {}, error);
    for (quint64 id : inFlight)
        emit debugAdapterResult(id, {}, error);
}
void ExtensionManager::sendDebugAdapterRequest(Entry& entry, quint64 id, const QString& type,
                                               const QJsonObject& session) {
    entry.debugRequestContexts.insert(id, session);
    QJsonObject message{{"command", "resolveDebugAdapter"},
                        {"requestId", QString::number(id)},
                        {"type", type},
                        {"session", session}};
    try {
        for (const auto& item :
             entry.manifest.value("contributes").toObject().value("debuggers").toArray()) {
            const auto contribution = item.toObject();
            if (contribution.value("type").toString() != type)
                continue;
            const auto executable = debugAdapterExecutableFromContribution(
                contribution, QFileInfo(entry.info.path).absolutePath(),
                currentDebugAdapterPlatform(), qEnvironmentVariableIsSet("PROCESSOR_ARCHITEW6432"));
            if (executable)
                message.insert("executable", wire::encodeDebugAdapterDescriptor(*executable));
            break;
        }
        entry.inFlightDebugRequests.insert(id);
        sendHostMessage(entry, message);
    } catch (const std::exception& error) {
        entry.inFlightDebugRequests.remove(id);
        const auto failureMessage = QString::fromUtf8(error.what());
        QTimer::singleShot(0, this, [this, id, failureMessage] {
            emit debugAdapterResult(id, {}, failureMessage);
        });
    }
}
void ExtensionManager::failCommandRequests(Entry& entry, const QString& error) {
    for (const auto& request : std::as_const(entry.pendingCommands))
        emit commandResult(request.id, {}, error);
    entry.pendingCommands.clear();
    for (quint64 id : std::as_const(entry.inFlightCommands))
        emit commandResult(id, {}, error);
    entry.inFlightCommands.clear();
}
void ExtensionManager::failChatRequests(Entry& entry, const QString& error) {
    const auto pending = std::exchange(entry.pendingChats, {});
    const auto inFlight = std::exchange(entry.inFlightChats, {});
    for (const auto& request : pending) {
        chatStreams_.finish(request.id);
        emit chatRequestFinished(request.id, {}, error);
    }
    for (auto it = inFlight.cbegin(); it != inFlight.cend(); ++it) {
        chatStreams_.finish(it.key());
        emit chatRequestFinished(it.key(), {}, error);
    }
    QVector<quint64> unfinishedFollowups;
    for (auto it = completedChats_.begin(); it != completedChats_.end();) {
        if (it.value().owner != &entry) {
            ++it;
            continue;
        }
        if (!it.value().followupsDelivered)
            unfinishedFollowups.append(it.key());
        it = completedChats_.erase(it);
    }
    for (quint64 id : unfinishedFollowups)
        emit chatFollowupsReady(id, {}, error);
}
void ExtensionManager::dispatchQueuedChatRequests(Entry& entry) {
    if (!entry.info.active)
        return;
    for (qsizetype index = 0; index < entry.pendingChats.size();) {
        const auto& pending = entry.pendingChats.at(index);
        if (!entry.runtimeChatParticipants.contains(pending.participantId)) {
            ++index;
            continue;
        }
        auto request = entry.pendingChats.takeAt(index);
        const auto id = request.id;
        entry.inFlightChats.insert(id, request);
        sendHostMessage(entry, {{"command", "chatRequest"},
                                {"requestId", QString::number(id)},
                                {"participantId", request.participantId},
                                {"request", request.request},
                                {"history", request.contextHistory}});
    }
}
void ExtensionManager::discardForwardedCommandRequests(Entry& source) {
    for (auto it = forwardedCommandRequests_.begin(); it != forwardedCommandRequests_.end();) {
        if (it->source == &source)
            it = forwardedCommandRequests_.erase(it);
        else
            ++it;
    }
    source.clientCommandRequestIds.clear();
}
void ExtensionManager::handleHostMessage(Entry& entry, const QJsonObject& message) {
    const auto event = message.value("event").toString();
    if (!entry.authenticated) {
        if (event != "hello" || message.value("token").toString() != entry.token ||
            message.value("id").toString() != entry.info.id) {
            entry.info.error = "Extension host authentication failed.";
            entry.process->kill();
            return;
        }
        entry.authenticated = true;
        documents_->sendInitial(
            [this, &entry](const QJsonObject& snapshot) { sendHostMessage(entry, snapshot); });
        sendHostMessage(
            entry,
            {{"command", "initialize"},
             {"workspace", workspace_},
             {"windowState", wire::encodeWindowState(windowState_)},
             {"editorState",
              wire::encodeEditorState(editorStateSource_ ? editorStateSource_() : editorState_)},
             {"environment", wire::encodeEnvironmentInfo(environment_->info())},
             {"configuration", wire::encodeConfigurationModel(configurationService_->model())}});
        return;
    }
    if (!entry.workspaceEditTransfer.transferring() && !entry.clipboardTransfer.transferring() &&
        (message.value("event") == QJsonValue("editorApplyEdit") ||
         entry.editorEditTransfer.recognizes(message) || entry.editorEditTransfer.transferring())) {
        QString error;
        const auto complete = message.value("event") == QJsonValue("editorApplyEdit") &&
                                      !entry.editorEditTransfer.transferring()
                                  ? std::optional<QJsonObject>(message)
                                  : entry.editorEditTransfer.accept(message, &error);
        if (!error.isEmpty()) {
            entry.info.error = error;
            entry.process->kill();
        } else if (complete) {
            // Direct messages need no reconstruction; avoid recursively decoding
            // them through this same transfer route.
            const auto requestId = complete->value("requestId").toString();
            bool valid = false;
            const auto request = requestId.toULongLong(&valid);
            const auto edit = complete->value("edit").isObject()
                                  ? wire::decodeTextEditorEdit(complete->value("edit").toObject())
                                  : std::nullopt;
            if (!valid || !request || QString::number(request) != requestId || !edit ||
                !complete->value("editorId").isString() ||
                complete->value("editorId").toString().isEmpty() ||
                complete->value("editorId").toString().size() > 256) {
                entry.info.error = "Invalid text editor edit request.";
                entry.process->kill();
                return;
            }
            const QPointer<QLocalSocket> origin(entry.socket);
            bool applied = false;
            try {
                if (!entry.stopping && entry.info.enabled && editorEdit_)
                    applied = editorEdit_(complete->value("editorId").toString(), *edit);
            } catch (...) {
                applied = false;
            }
            // Synchronize editor context before settling the caller, just as
            // document notifications are published by the edit delegate first.
            if (editorStateSource_)
                setEditorState(editorStateSource_());
            if (origin && origin->state() == QLocalSocket::ConnectedState)
                origin->write(wire::encode({{"command", "workspaceResult"},
                                            {"requestId", requestId},
                                            {"result", QJsonObject{{"applied", applied}}},
                                            {"error", QString{}}}));
        }
        return;
    }
    if (!entry.workspaceEditTransfer.transferring() &&
        (entry.clipboardTransfer.recognizes(message) || entry.clipboardTransfer.transferring())) {
        QString error;
        const auto request = entry.clipboardTransfer.accept(message, &error);
        if (!error.isEmpty()) {
            entry.info.error = error;
            entry.process->kill();
        } else if (request)
            handleHostMessage(entry, *request);
        return;
    }
    const bool editTransferEvent = event == "workspaceEditBegin" || event == "workspaceEditChunk" ||
                                   event == "workspaceEditEnd";
    if (editTransferEvent || entry.workspaceEditTransfer.transferring()) {
        QString error;
        const auto request = entry.workspaceEditTransfer.accept(message, &error);
        if (!error.isEmpty()) {
            entry.info.error = error;
            entry.process->kill();
        } else if (request)
            handleHostMessage(entry, *request);
        return;
    }
    if (event == "clipboard") {
        if (entry.stopping)
            return;
        const QPointer<QLocalSocket> origin(entry.socket);
        const auto requestId = message.value("requestId").toString();
        if (!clipboard_.accept(
                message, [origin, requestId](const QJsonValue& result, const QString& error) {
                    if (!origin || origin->state() != QLocalSocket::ConnectedState)
                        return;
                    try {
                        wire::sendJsonTransfer(
                            {{"command", "clipboardResult"},
                             {"requestId", requestId},
                             {"result", result},
                             {"error", error}},
                            {"command", "clipboardResult", "clipboardResult"},
                            [origin](const QJsonObject& frame) {
                                if (!origin || origin->state() != QLocalSocket::ConnectedState ||
                                    origin->write(wire::encode(frame)) < 0)
                                    throw std::runtime_error("Clipboard reply connection closed.");
                            });
                    } catch (...) {
                        if (origin && origin->state() == QLocalSocket::ConnectedState)
                            origin->write(
                                wire::encode({{"command", "clipboardResult"},
                                              {"requestId", requestId},
                                              {"result", QJsonValue(QJsonValue::Null)},
                                              {"error", "Could not transfer clipboard result."}}));
                    }
                })) {
            entry.info.error = "Invalid clipboard request.";
            entry.process->kill();
        }
        return;
    }
    if (event == "progress") {
        if (entry.stopping)
            return;
        if (!progress_->accept(entry.info.id, entry.info.name, message)) {
            entry.info.error = "Invalid extension progress message.";
            entry.process->kill();
        }
        return;
    }
    if (event == "externalUri") {
        if (entry.stopping)
            return;
        const QPointer<QLocalSocket> origin(entry.socket);
        if (!externalUris_->accept(entry.info.id, message, [origin](const QJsonObject& reply) {
                if (origin && origin->state() == QLocalSocket::ConnectedState)
                    origin->write(wire::encode(reply));
            })) {
            entry.info.error = "Invalid external URI request.";
            entry.process->kill();
        }
        return;
    }
    if (event == "uriHandler") {
        if (entry.stopping)
            return;
        const QPointer<QLocalSocket> origin(entry.socket);
        if (!uriHandlers_->accept(entry.info.id, message, [origin](const QJsonObject& reply) {
                if (origin && origin->state() == QLocalSocket::ConnectedState)
                    origin->write(wire::encode(reply));
            })) {
            entry.info.error = "Invalid URI handler registration.";
            entry.process->kill();
        }
        return;
    }
    if (event == "nativeView") {
        if (entry.stopping)
            return;
        if (!nativeViews_.accept(entry.info.id, message)) {
            entry.info.error = "Invalid native extension view message.";
            entry.process->kill();
        }
        return;
    }
    if (event == "statusBar") {
        if (entry.stopping)
            return;
        if (!statusBar_->accept(entry.info.id, entry.info.name, message)) {
            entry.info.error = "Invalid extension status bar update.";
            entry.process->kill();
        }
        return;
    }
    if (event == "quickPick") {
        if (entry.stopping)
            return;
        const QPointer<QLocalSocket> origin(entry.socket);
        if (!quickPick_->accept(entry.info.id, message, [origin](const QJsonObject& reply) {
                if (origin && origin->state() == QLocalSocket::ConnectedState)
                    origin->write(wire::encode(reply));
            })) {
            entry.info.error = "Invalid extension quick pick request.";
            entry.process->kill();
        }
        return;
    }
    if (event == "inputBox") {
        if (entry.stopping)
            return;
        const QPointer<QLocalSocket> origin(entry.socket);
        if (!inputBox_->accept(entry.info.id, message, [origin](const QJsonObject& reply) {
                if (origin && origin->state() == QLocalSocket::ConnectedState)
                    origin->write(wire::encode(reply));
            })) {
            entry.info.error = "Invalid extension input box request.";
            entry.process->kill();
        }
        return;
    }
    if (event == "windowFileDialog") {
        if (entry.stopping)
            return;
        const QPointer<ExtensionManager> alive(this);
        const QPointer<QLocalSocket> origin(entry.socket);
        const auto requestId = message.value("requestId").toString();
        const auto reply = [alive, origin, requestId](std::optional<QVector<QUrl>> selection,
                                                      const QString& error) {
            if (!alive || !origin || origin->state() != QLocalSocket::ConnectedState)
                return;
            QJsonValue resources(QJsonValue::Null);
            if (selection) {
                QJsonArray values;
                for (const auto& uri : *selection)
                    values.append(uri.toString(QUrl::FullyEncoded));
                resources = values;
            }
            origin->write(wire::encode({{"command", "windowFileDialogResult"},
                                        {"requestId", requestId},
                                        {"resources", resources},
                                        {"error", error}}));
        };
        if (!dialogs_->accept(entry.info.id, message, reply)) {
            entry.info.error = "Invalid extension file dialog request.";
            entry.process->kill();
        }
        return;
    }
    if (event == "windowMessage") {
        if (entry.stopping)
            return;
        const QPointer<ExtensionManager> alive(this);
        const QPointer<QLocalSocket> origin(entry.socket);
        const auto requestId = message.value("requestId").toString();
        const auto reply = [alive, origin, requestId](std::optional<int> selection,
                                                      const QString& error) {
            if (!alive || !origin || origin->state() != QLocalSocket::ConnectedState)
                return;
            const QJsonObject result{
                {"command", "windowMessageResult"},
                {"requestId", requestId},
                {"selection", selection ? QJsonValue(*selection) : QJsonValue(QJsonValue::Null)},
                {"error", error}};
            origin->write(wire::encode(result));
        };
        if (!messages_->accept(entry.info.id, entry.info.name, message, reply)) {
            entry.info.error = "Invalid extension window message.";
            entry.process->kill();
        }
        return;
    }
    if (event == "output") {
        if (entry.stopping)
            return;
        if (!outputs_->accept(entry.info.id, message)) {
            entry.info.error = "Invalid extension output message.";
            entry.process->kill();
        }
        return;
    }
    if (event == "registerTextContentProvider" || event == "unregisterTextContentProvider") {
        bool valid = false;
        const auto encoded = message.value("handle").toString();
        const auto handle = encoded.toULongLong(&valid);
        const bool accepted =
            valid && handle && QString::number(handle) == encoded &&
            (event == "registerTextContentProvider"
                 ? message.value("scheme").isString() &&
                       contentProviders_->registerProvider(entry.info.id, handle,
                                                           message.value("scheme").toString())
                 : contentProviders_->unregisterProvider(entry.info.id, handle));
        if (!accepted) {
            entry.info.error = "Invalid text content provider registration.";
            entry.process->kill();
        }
        return;
    }
    if (event == "textContentBegin" || event == "textContentChunk" || event == "textContentEnd") {
        QString error;
        if (!contentProviders_->accept(entry.info.id, message, &error)) {
            entry.info.error = error;
            entry.process->kill();
        }
        return;
    }
    if (event == "workspaceApplyEdit") {
        bool valid = false;
        const auto requestId = message.value("requestId").toString();
        const auto id = requestId.toULongLong(&valid);
        const auto edit = wire::decodeWorkspaceEdit(message);
        if (!valid || !id || QString::number(id) != requestId || !edit) {
            entry.info.error = "Invalid workspace edit request.";
            entry.process->kill();
            return;
        }
        const QPointer<QLocalSocket> origin(entry.socket);
        const auto reply = [origin, requestId](bool applied) {
            if (origin && origin->state() == QLocalSocket::ConnectedState)
                origin->write(wire::encode({{"command", "workspaceResult"},
                                            {"requestId", requestId},
                                            {"result", QJsonObject{{"applied", applied}}},
                                            {"error", QString{}}}));
        };
        try {
            if (entry.stopping || !entry.info.enabled) {
                reply(false);
                return;
            }
            auto* watcher = new QFutureWatcher<bool>(this);
            connect(watcher, &QFutureWatcher<bool>::finished, this, [watcher, reply] {
                try {
                    reply(watcher->result());
                } catch (...) {
                    reply(false);
                }
                watcher->deleteLater();
            });
            try {
                watcher->setFuture(documents_->applyEdit(entry.info.id, *edit));
            } catch (...) {
                watcher->deleteLater();
                reply(false);
            }
        } catch (...) {
            reply(false);
        }
        return;
    }
    if (event == "workspaceOpenDocument" || event == "workspaceCreateDocument") {
        bool valid = false;
        const auto requestId = message.value("requestId").toString();
        const auto id = requestId.toULongLong(&valid);
        const auto optionalString = [&message](const char* key) {
            return !message.contains(key) || message.value(key).isString();
        };
        if (!valid || !id || QString::number(id) != requestId || !optionalString("encoding") ||
            (event == "workspaceOpenDocument" && !message.value("uri").isString()) ||
            (event == "workspaceCreateDocument" &&
             (!optionalString("language") || !optionalString("content")))) {
            entry.info.error = "Invalid document opening request.";
            entry.process->kill();
            return;
        }
        const QPointer<QLocalSocket> origin(entry.socket);
        const auto reply = [origin, requestId](const QJsonObject& result, const QString& error) {
            if (origin && origin->state() == QLocalSocket::ConnectedState)
                origin->write(wire::encode({{"command", "workspaceResult"},
                                            {"requestId", requestId},
                                            {"result", result},
                                            {"error", error}}));
        };
        try {
            if (entry.stopping || !entry.info.enabled)
                throw std::runtime_error("Extension is disabled or stopping.");
            const auto encoding = message.contains("encoding")
                                      ? std::optional(message.value("encoding").toString())
                                      : std::nullopt;
            QFuture<TextDocumentSnapshot> future;
            if (event == "workspaceOpenDocument") {
                const QUrl uri(message.value("uri").toString());
                if (!uri.isValid() || uri.scheme().isEmpty())
                    throw std::invalid_argument("Invalid document URI.");
                future = documents_->open(entry.info.id, uri, {encoding});
            } else {
                future = documents_->create(
                    entry.info.id, {message.contains("language")
                                        ? std::optional(message.value("language").toString())
                                        : std::nullopt,
                                    message.contains("content")
                                        ? std::optional(message.value("content").toString())
                                        : std::nullopt,
                                    encoding});
            }
            auto* watcher = new QFutureWatcher<TextDocumentSnapshot>(this);
            connect(
                watcher, &QFutureWatcher<TextDocumentSnapshot>::finished, this,
                [this, watcher, reply, origin] {
                    try {
                        if (!origin || origin->state() != QLocalSocket::ConnectedState) {
                            watcher->deleteLater();
                            return;
                        }
                        if (watcher->future().isCanceled())
                            throw std::runtime_error("Document opening canceled.");
                        const auto snapshot = watcher->result();
                        // The snapshot frames must precede the reply on the same
                        // socket, so the host resolves to its live mirrored object.
                        if (!documents_->contains(snapshot.id, snapshot.uri))
                            throw std::runtime_error(
                                "Document closed or changed resource before opening completed.");
                        reply({{"documentId", snapshot.id}}, {});
                    } catch (const std::exception& exception) {
                        reply({}, QString::fromUtf8(exception.what()));
                    } catch (...) {
                        reply({}, "Document opening failed.");
                    }
                    watcher->deleteLater();
                });
            watcher->setFuture(future);
        } catch (const std::exception& exception) {
            reply({}, QString::fromUtf8(exception.what()));
        } catch (...) {
            reply({}, "Document opening failed.");
        }
        return;
    }
    if (event == "workspaceSave" || event == "workspaceSaveAll") {
        const QPointer<ExtensionManager> alive(this);
        const QPointer<QLocalSocket> origin(entry.socket);
        bool valid = false;
        const auto requestId = message.value("requestId").toString();
        const auto id = requestId.toULongLong(&valid);
        if (!valid || id == 0 || QString::number(id) != requestId ||
            (event == "workspaceSave" &&
             (!message.value("uri").isString() || !message.value("saveAs").isBool())) ||
            (event == "workspaceSaveAll" && !message.value("includeUntitled").isBool())) {
            entry.info.error = "Invalid workspace save request.";
            entry.process->kill();
            return;
        }
        QString error;
        QJsonObject result;
        if (entry.stopping || !entry.info.enabled)
            error = "Extension is disabled or stopping.";
        else {
            try {
                if (event == "workspaceSaveAll")
                    result.insert("saved", documents_->saveAll(
                                               message.value("includeUntitled").toBool(), &error));
                else {
                    const QUrl uri(message.value("uri").toString());
                    if (!uri.isValid() || uri.isEmpty() || uri.scheme().isEmpty())
                        error = "Invalid resource URI.";
                    else {
                        const auto saved =
                            documents_->saveResource(uri, message.value("saveAs").toBool(), &error);
                        result.insert("uri", saved ? QJsonValue(saved->toString(QUrl::FullyEncoded))
                                                   : QJsonValue(QJsonValue::Null));
                    }
                }
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
            } catch (...) {
                error = "Workspace save failed.";
            }
        }
        // A save-as dialog runs an event loop: the extension may be disabled
        // or restarted while it is open. Never reply into its replacement host.
        if (!alive || !origin || entry.socket != origin.data())
            return;
        sendHostMessage(entry, {{"command", "workspaceResult"},
                                {"requestId", requestId},
                                {"result", result},
                                {"error", error}});
    } else if (event == "documentSave") {
        const QPointer<ExtensionManager> alive(this);
        const QPointer<QLocalSocket> origin(entry.socket);
        bool valid = false;
        const auto requestId = message.value("requestId").toString();
        const auto id = requestId.toULongLong(&valid);
        if (!valid || id == 0 || QString::number(id) != requestId ||
            !message.value("documentId").isString()) {
            entry.info.error = "Invalid document save request.";
            entry.process->kill();
            return;
        }
        QString error;
        bool saved = false;
        if (entry.stopping || !entry.info.enabled)
            error = "Extension is disabled or stopping.";
        else {
            try {
                saved = documents_->save(message.value("documentId").toString(), &error);
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
            } catch (...) {
                error = "Document save failed.";
            }
        }
        if (alive && origin && origin->state() == QLocalSocket::ConnectedState)
            origin->write(wire::encode({{"command", "documentSaveResult"},
                                        {"requestId", requestId},
                                        {"saved", saved},
                                        {"error", error}}));
    } else if (event == "configurationUpdate") {
        bool valid = false;
        const auto textId = message.value("requestId").toString();
        const auto requestId = textId.toULongLong(&valid);
        if (!valid || requestId == 0 || QString::number(requestId) != textId) {
            entry.info.error = "Invalid configuration request identity.";
            entry.process->kill();
            return;
        }
        const auto error = entry.stopping || !entry.info.enabled
                               ? QStringLiteral("Extension is disabled or stopping.")
                               : applyConfigurationUpdate(*configurationService_, message);
        // Ensure this caller's snapshot is current before its future resolves,
        // even when the update was a no-op and produced no global change event.
        sendHostMessage(
            entry, {{"command", "configuration"},
                    {"model", wire::encodeConfigurationModel(configurationService_->model())}});
        sendHostMessage(
            entry,
            {{"command", "configurationUpdateResult"}, {"requestId", textId}, {"error", error}});
    } else if (event == "registeredCommand") {
        const auto id = message.value("id").toString();
        bool claimedByAnotherExtension = false;
        claimedByAnotherExtension = workbenchCommandIds_ && workbenchCommandIds_().contains(id);
        for (const auto& other : entries_) {
            if (other.get() == &entry || !other->info.enabled || !other->info.error.isEmpty())
                continue;
            claimedByAnotherExtension |= other->runtimeCommands.contains(id);
            for (const auto& value :
                 other->manifest.value("contributes").toObject().value("commands").toArray())
                claimedByAnotherExtension |= value.toObject().value("command").toString() == id;
        }
        if (id.trimmed().isEmpty() || id != id.trimmed() || entry.runtimeCommands.contains(id) ||
            claimedByAnotherExtension) {
            entry.info.error = claimedByAnotherExtension
                                   ? "Duplicate extension command ID: " + id
                                   : "Extension host registered an invalid command.";
            entry.process->kill();
            return;
        }
        entry.runtimeCommands.insert(id);
        emit changed();
    } else if (event == "unregisteredCommand") {
        entry.runtimeCommands.remove(message.value("id").toString());
        emit changed();
    } else if (event == "registeredFormatter") {
        const auto language = message.value("language").toString();
        bool contributed = false;
        for (const auto& value :
             entry.manifest.value("contributes").toObject().value("formatters").toArray())
            contributed |= value.toObject().value("language").toString() == language;
        if (!contributed) {
            entry.info.error = "Extension registered an undeclared formatter.";
            entry.process->kill();
            return;
        }
        entry.runtimeFormatters.insert(language);
    } else if (event == "unregisteredFormatter") {
        entry.runtimeFormatters.remove(message.value("language").toString());
    } else if (event == "registeredDebugAdapterDescriptorFactory") {
        const auto type = message.value("type").toString();
        bool contributed = false;
        for (const auto& value :
             entry.manifest.value("contributes").toObject().value("debuggers").toArray())
            contributed |= value.toObject().value("type").toString() == type;
        if (!contributed) {
            entry.info.error = "Extension registered an undeclared debug adapter factory.";
            entry.process->kill();
            return;
        }
        entry.runtimeDebugAdapterFactories.insert(type);
    } else if (event == "unregisteredDebugAdapterDescriptorFactory") {
        entry.runtimeDebugAdapterFactories.remove(message.value("type").toString());
    } else if (event == "registeredChatParticipant") {
        const auto id = message.value("id").toString();
        bool contributed = false;
        for (const auto& value :
             entry.manifest.value("contributes").toObject().value("chatParticipants").toArray())
            contributed |= value.toObject().value("id").toString() == id;
        if (!contributed || entry.runtimeChatParticipants.contains(id)) {
            entry.info.error = "Extension registered an undeclared or duplicate chat participant.";
            entry.process->kill();
            return;
        }
        entry.runtimeChatParticipants.insert(id);
        dispatchQueuedChatRequests(entry);
    } else if (event == "unregisteredChatParticipant") {
        const auto id = message.value("id").toString();
        entry.runtimeChatParticipants.remove(id);
        entry.runtimeChatIcons.remove(id);
        entry.runtimeChatFollowups.remove(id);
        // A disposed participant cannot accept requests still waiting for it.
        for (qsizetype index = 0; index < entry.pendingChats.size();) {
            if (entry.pendingChats.at(index).participantId != id) {
                ++index;
                continue;
            }
            const auto request = entry.pendingChats.takeAt(index);
            chatStreams_.finish(request.id);
            emit chatRequestFinished(request.id, {}, "Chat participant was disposed.");
        }
        for (auto it = entry.inFlightChats.begin(); it != entry.inFlightChats.end();) {
            if (it.value().participantId != id) {
                ++it;
                continue;
            }
            const auto requestId = it.key();
            it = entry.inFlightChats.erase(it);
            chatStreams_.cancel(requestId);
            chatStreams_.finish(requestId);
            sendHostMessage(entry,
                            {{"command", "chatCancel"}, {"requestId", QString::number(requestId)}});
            emit chatRequestFinished(requestId, {}, "Chat participant was disposed.");
        }
        QVector<quint64> unfinishedFollowups;
        for (auto it = completedChats_.begin(); it != completedChats_.end();) {
            if (it.value().owner != &entry || it.value().participantId != id) {
                ++it;
                continue;
            }
            sendHostMessage(entry, {{"command", "chatFollowupCancel"},
                                    {"requestId", QString::number(it.key())}});
            if (!it.value().followupsDelivered)
                unfinishedFollowups.append(it.key());
            it = completedChats_.erase(it);
        }
        for (quint64 requestId : unfinishedFollowups)
            emit chatFollowupsReady(requestId, {}, "Chat participant was disposed.");
        emit changed();
    } else if (event == "chatParticipantIconChanged") {
        const auto id = message.value("id").toString();
        const auto icon = message.value("iconPath");
        const auto iconObject = icon.toObject();
        const bool lightAndDark = iconObject.value("light").isString() &&
                                  !iconObject.value("light").toString().isEmpty() &&
                                  iconObject.value("dark").isString() &&
                                  !iconObject.value("dark").toString().isEmpty();
        const bool themeIcon =
            iconObject.value("id").isString() && !iconObject.value("id").toString().isEmpty() &&
            (!iconObject.contains("color") ||
             (iconObject.value("color").isObject() &&
              !iconObject.value("color").toObject().value("id").toString().isEmpty()));
        const bool validIcon = icon.isUndefined() || icon.isNull() ||
                               (icon.isString() && !icon.toString().isEmpty()) ||
                               (icon.isObject() && (lightAndDark || themeIcon));
        if (!entry.runtimeChatParticipants.contains(id) || !validIcon ||
            !message.value("hasFollowups").isBool()) {
            entry.info.error = "Extension sent an invalid chat participant icon.";
            entry.process->kill();
            return;
        }
        if (icon.isUndefined() || icon.isNull())
            entry.runtimeChatIcons.remove(id);
        else
            entry.runtimeChatIcons.insert(id, icon);
        entry.runtimeChatFollowups.insert(id, message.value("hasFollowups").toBool());
        emit changed();
    } else if (event == "ready") {
        entry.info.active = true;
        for (const auto& snapshot : debugSessions_->snapshot())
            sendHostMessage(entry, snapshot);
        debugConfigurations_->ready(entry.info.id);
        const auto commands = std::exchange(entry.pendingCommands, {});
        for (const auto& request : commands) {
            entry.inFlightCommands.insert(request.id);
            sendHostMessage(entry, {{"command", "execute"},
                                    {"id", request.command},
                                    {"requestId", QString::number(request.id)},
                                    {"arguments", request.arguments}});
        }
        const auto formats = std::exchange(entry.pendingFormats, {});
        for (const auto& request : formats) {
            if (!entry.runtimeFormatters.contains(request.language)) {
                emit documentFormatResult(request.id, {}, "Formatter was not registered.");
                continue;
            }
            entry.inFlightFormats.insert(request.id);
            sendHostMessage(entry, {{"command", "format"},
                                    {"requestId", QString::number(request.id)},
                                    {"language", request.language},
                                    {"text", request.text}});
        }
        const auto debugRequests = std::exchange(entry.pendingDebugRequests, {});
        for (const auto& request : debugRequests) {
            sendDebugAdapterRequest(entry, request.id, request.type, request.session);
        }
        dispatchQueuedChatRequests(entry);
        emit changed();
    } else if (event == "failed") {
        entry.info.error = message.value("message").toString("Extension activation failed.");
        emit changed();
    } else if (event == "openFile") {
        emit openFileRequested(message.value("path").toString());
    } else if (event == "information") {
        emit informationMessage(message.value("message").toString());
    } else if (event == "executeCommandRequest") {
        bool valid = false;
        const auto sourceId = message.value("requestId").toString().toULongLong(&valid);
        const auto commandId = message.value("id").toString();
        const auto arguments = message.value("arguments");
        if (!valid || sourceId == 0 || commandId.trimmed().isEmpty() || !arguments.isArray() ||
            entry.clientCommandRequestIds.contains(sourceId)) {
            entry.info.error = "Invalid extension command request.";
            entry.process->kill();
            return;
        }
        entry.clientCommandRequestIds.insert(sourceId);
        const auto downstreamId = requestCommand(commandId, arguments.toArray(), entry.info.id);
        if (downstreamId == 0) {
            entry.clientCommandRequestIds.remove(sourceId);
            const bool builtin = workbenchCommandIds_ && workbenchCommandIds_().contains(commandId);
            if (builtin && arguments.toArray().isEmpty() && invokeWorkbenchCommand_ &&
                invokeWorkbenchCommand_(commandId)) {
                sendHostMessage(entry, {{"command", "extensionCommandResult"},
                                        {"requestId", QString::number(sourceId)}});
                return;
            }
            sendHostMessage(
                entry, {{"command", "extensionCommandResult"},
                        {"requestId", QString::number(sourceId)},
                        {"error", builtin && !arguments.toArray().isEmpty()
                                      ? "Workbench command does not accept arguments: " + commandId
                                      : "Command not found or unavailable: " + commandId}});
        } else {
            forwardedCommandRequests_.insert(downstreamId, {&entry, sourceId});
        }
    } else if (event == "getCommandsRequest") {
        bool valid = false;
        const auto sourceId = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || sourceId == 0 || !message.value("filterInternal").isBool()) {
            entry.info.error = "Invalid extension command list request.";
            entry.process->kill();
            return;
        }
        QJsonArray ids;
        for (const auto& id : commands())
            if (!message.value("filterInternal").toBool() || !id.startsWith('_'))
                ids.append(id);
        sendHostMessage(entry, {{"command", "getCommandsResult"},
                                {"requestId", QString::number(sourceId)},
                                {"commands", ids}});
    } else if (event == "commandResult") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || !entry.inFlightCommands.remove(id))
            return;
        QString error;
        if (!message.value("ok").toBool()) {
            error = message.value("error").toString();
            if (error.isEmpty())
                error = "Extension command failed.";
        }
        emit commandResult(id, message.value("result"), error);
        if (!error.isEmpty())
            emit informationMessage(error);
    } else if (event == "formatResult") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || !entry.inFlightFormats.remove(id))
            return;
        emit documentFormatResult(id, message.value("text").toString(),
                                  message.value("error").toString());
    } else if (event == "debugStartRequest") {
        if (entry.stopping)
            return;
        const auto owner = entry.info.id;
        if (!debugStarts_->request(owner, message, [this, owner](const QJsonObject& response) {
                for (const auto& candidate : entries_)
                    if (candidate->info.id == owner && candidate->authenticated &&
                        !candidate->stopping) {
                        sendHostMessage(*candidate, response);
                        return;
                    }
            })) {
            entry.info.error = "Extension sent an invalid debug start request.";
            entry.process->kill();
        }
    } else if (event == "debugSessionRequest") {
        if (entry.stopping)
            return;
        const auto owner = entry.info.id;
        if (!debugSessions_->request(owner, message, [this, owner](const QJsonObject& response) {
                for (const auto& candidate : entries_)
                    if (candidate->info.id == owner && candidate->authenticated &&
                        !candidate->stopping) {
                        sendHostMessage(*candidate, response);
                        return;
                    }
            })) {
            entry.info.error = "Extension sent an invalid debug session request.";
            entry.process->kill();
        }
    } else if (event == "registeredDebugConfigurationProvider") {
        if (entry.stopping)
            return;
        if (!debugConfigurations_->registerProvider(entry.info.id, message)) {
            entry.info.error = "Invalid debug configuration provider registration.";
            entry.process->kill();
        }
    } else if (event == "unregisteredDebugConfigurationProvider") {
        if (entry.stopping)
            return;
        bool valid = false;
        const auto text = message.value("handle").toString();
        const auto handle = text.toULongLong(&valid);
        if (!valid || !handle || QString::number(handle) != text ||
            !debugConfigurations_->unregisterProvider(entry.info.id, handle)) {
            entry.info.error = "Invalid debug configuration provider disposal.";
            entry.process->kill();
        }
    } else if (event == "debugConfigurationResult") {
        if (entry.stopping)
            return;
        if (!debugConfigurations_->accept(entry.info.id, message)) {
            entry.info.error = "Invalid debug configuration provider result.";
            entry.process->kill();
        }
    } else if (event == "registeredDebugAdapterTrackerFactory") {
        if (!entry.stopping && !debugTrackers_->registerFactory(entry.info.id, message)) {
            entry.info.error = "Invalid debug adapter tracker registration.";
            entry.process->kill();
        }
    } else if (event == "unregisteredDebugAdapterTrackerFactory" || event == "debugTrackersReady") {
        bool valid{};
        const auto field = event == "debugTrackersReady" ? "requestId" : "handle";
        const auto text = message.value(field).toString();
        const auto id = text.toULongLong(&valid);
        const bool accepted =
            valid && id && QString::number(id) == text &&
            (event == "debugTrackersReady" ? debugTrackers_->acceptReady(entry.info.id, id)
                                           : debugTrackers_->unregisterFactory(entry.info.id, id));
        if (!entry.stopping && !accepted) {
            entry.info.error = "Invalid debug adapter tracker response.";
            entry.process->kill();
        }
    } else if (event == "debugTrackerDiagnostic") {
        if (message.value("message").isString())
            emit informationMessage(entry.info.id + ": " + message.value("message").toString());
    } else if (event == "debugAdapterResult") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || !entry.inFlightDebugRequests.remove(id))
            return;
        const auto error = message.value("error").toString();
        const auto descriptor = message.value("descriptor").toObject();
        const auto context = entry.debugRequestContexts.take(id);
        if (!error.isEmpty())
            emit debugAdapterResult(id, {}, error);
        else if (!wire::decodeDebugAdapterDescriptor(descriptor))
            emit debugAdapterResult(id, {}, "Invalid debug adapter descriptor.");
        else {
            if (descriptor.value("type") == QJsonValue("implementation"))
                entry.activeInlineAdapters.insert(id);
            debugTrackers_->prepare(id, entry.info.id, context, descriptor);
        }
    } else if (event == "chatPart") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || !entry.inFlightChats.contains(id))
            return;
        if (!message.value("part").isObject()) {
            entry.info.error = "Extension sent an invalid chat response part.";
            entry.process->kill();
            return;
        }
        const auto part = message.value("part").toObject();
        const auto accepted = chatStreams_.append(id, part);
        if (!accepted) {
            entry.info.error = "Extension sent an invalid chat response part.";
            entry.process->kill();
            return;
        }
        entry.inFlightChats[id].parts.append(part);
        emit chatResponsePart(id, accepted->sequence, part);
    } else if (event == "chatResult") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid || !entry.inFlightChats.contains(id))
            return;
        if (message.contains("result") && !message.value("result").isObject()) {
            entry.info.error = "Extension sent an invalid chat result.";
            entry.process->kill();
            return;
        }
        auto request = entry.inFlightChats.take(id);
        chatStreams_.finish(id);
        const auto result = message.value("result").toObject();
        const auto error = message.value("error").toString();
        if (error.isEmpty())
            chatHistories_[request.sessionId].record(request.participantId, request.request,
                                                     request.parts, result);
        if (error.isEmpty())
            completedChats_.insert(id, {&entry, request.sessionId, request.participantId, result});
        emit chatRequestFinished(id, result, error);
    } else if (event == "chatFollowups") {
        bool valid = false;
        const auto id = message.value("requestId").toString().toULongLong(&valid);
        if (!valid)
            return;
        auto it = completedChats_.find(id);
        if (it == completedChats_.end() || it->owner != &entry || it->followupsDelivered)
            return;
        if (!message.value("followups").isArray() || !message.value("error").isString()) {
            entry.info.error = "Extension sent invalid chat followups.";
            entry.process->kill();
            return;
        }
        QSet<QString> ownedParticipants;
        for (const auto& value :
             entry.manifest.value("contributes").toObject().value("chatParticipants").toArray())
            ownedParticipants.insert(value.toObject().value("id").toString());
        const auto followups = message.value("followups").toArray();
        for (const auto& value : followups) {
            const auto item = value.toObject();
            if (!value.isObject() || item.value("prompt").toString().trimmed().isEmpty() ||
                (item.contains("label") && !item.value("label").isString()) ||
                (item.contains("participant") && !item.value("participant").isString()) ||
                (item.contains("command") && !item.value("command").isString())) {
                it->followupsDelivered = true;
                emit chatFollowupsReady(id, {}, "Invalid chat followup.");
                return;
            }
            if (item.contains("participant") &&
                !ownedParticipants.contains(item.value("participant").toString())) {
                it->followupsDelivered = true;
                emit chatFollowupsReady(
                    id, {}, "Followup must target a participant from the same extension.");
                return;
            }
        }
        it->followupsDelivered = true;
        emit chatFollowupsReady(id, followups, message.value("error").toString());
    }
}
bool ExtensionManager::setEnabled(const QString& id, bool enabled) {
    for (auto& entry : entries_) {
        if (entry->info.id != id)
            continue;
        if (entry->transitioning || entry->info.uninstalling)
            return false;
        // A command collision is transient: another extension can be disabled.
        // Revalidate the manifest on retry without clearing unrelated host or
        // binary-integrity failures.
        if (enabled &&
            (entry->info.error.startsWith("Duplicate contributed command ID: ") ||
             entry->info.error.startsWith("Duplicate contributed chat participant ID: ") ||
             entry->info.error.startsWith("Duplicate contributed native view ID: ")))
            entry->info.error = validateManifest(entry->manifest);
        if (enabled && !entry->info.error.isEmpty())
            return false;
        if (enabled) {
            const auto workbenchIds = workbenchCommandIds_ ? workbenchCommandIds_() : QStringList{};
            for (const auto& value :
                 entry->manifest.value("contributes").toObject().value("commands").toArray()) {
                const auto commandId = value.toObject().value("command").toString();
                if (workbenchIds.contains(commandId)) {
                    entry->info.error = "Duplicate contributed command ID: " + commandId;
                    emit changed();
                    return false;
                }
            }
            for (const auto& other : entries_) {
                if (other.get() == entry.get() || !other->info.enabled ||
                    !other->info.error.isEmpty())
                    continue;
                const auto conflict = duplicateContributedCommand(entry->manifest, other->manifest);
                if (!conflict.isEmpty()) {
                    entry->info.error = "Duplicate contributed command ID: " + conflict;
                    emit changed();
                    return false;
                }
                const auto participantConflict =
                    duplicateContributedParticipant(entry->manifest, other->manifest);
                if (!participantConflict.isEmpty()) {
                    entry->info.error =
                        "Duplicate contributed chat participant ID: " + participantConflict;
                    emit changed();
                    return false;
                }
                auto viewConflicts = contributedNativeViewIds(entry->manifest);
                viewConflicts.intersect(contributedNativeViewIds(other->manifest));
                if (!viewConflicts.isEmpty()) {
                    entry->info.error =
                        "Duplicate contributed native view ID: " + *viewConflicts.cbegin();
                    emit changed();
                    return false;
                }
            }
        }
        QDir().mkpath(directory_);
        QSettings settings(QDir(directory_).filePath("enabled.ini"), QSettings::IniFormat);
        if (enabled) {
            entry->trustedHash = libraryHash(entry->info.path);
            if (entry->trustedHash.isEmpty())
                return false;
            settings.setValue(id + "/trustedHash", entry->trustedHash);
        }
        settings.setValue(id + "/enabled", enabled);
        settings.sync();
        if (settings.status() != QSettings::NoError)
            return false;
        entry->info.enabled = enabled;
        rebuildConfigurationContributions();
        if (!enabled) {
            deactivate(*entry);
            entry->info.error = validateManifest(entry->manifest);
        } else if (entry->manifest.value("activationEvents")
                       .toArray()
                       .contains("onStartupFinished")) {
            if (!activate(*entry)) {
                entry->info.enabled = false;
                rebuildConfigurationContributions();
                settings.setValue(id + "/enabled", false);
                settings.sync();
                emit changed();
                return false;
            }
        }
        emit changed();
        return true;
    }
    return false;
}
bool ExtensionManager::uninstall(const QString& id) {
    // Never guess which package to remove when discovery found duplicate IDs.
    if (std::count_if(entries_.begin(), entries_.end(),
                      [&](const auto& e) { return e->info.id == id; }) != 1)
        return false;
    auto& entry = **std::find_if(entries_.begin(), entries_.end(),
                                 [&](const auto& e) { return e->info.id == id; });
    if (entry.transitioning || entry.info.uninstalling)
        return false;
    QSettings settings(QDir(directory_).filePath("enabled.ini"), QSettings::IniFormat);
    settings.setValue(id + "/enabled", false);
    settings.sync();
    if (settings.status() != QSettings::NoError)
        return false;
    entry.info.enabled = false;
    entry.info.uninstalling = true;
    const auto path = entry.info.path, version = entry.info.version, directory = directory_;
    deactivate(entry);
    rebuildConfigurationContributions();
    const auto remove = [this, id, path, version, directory] {
        auto* watcher = new QFutureWatcher<NativeExtensionRemovalResult>(this);
        connect(
            watcher, &QFutureWatcher<NativeExtensionRemovalResult>::finished, this,
            [this, watcher, id, path] {
                const auto result = watcher->result();
                watcher->deleteLater();
                const auto found =
                    std::find_if(entries_.begin(), entries_.end(),
                                 [&](const auto& e) { return e->info.path == path; });
                if (found == entries_.end())
                    return;
                QString diagnostic = result.error;
                if (result.removed) {
                    QSettings settings(QDir(directory_).filePath("enabled.ini"),
                                       QSettings::IniFormat);
                    settings.remove(id);
                    settings.sync();
                    if (settings.status() != QSettings::NoError)
                        diagnostic +=
                            " Extension removed, but its enablement record could not be cleared.";
                    entries_.erase(found);
                } else {
                    (*found)->info.uninstalling = false;
                    (*found)->info.error = diagnostic;
                }
                rebuildConfigurationContributions();
                emit changed();
                emit uninstallFinished(id, result.removed, diagnostic);
            });
        watcher->setFuture(QtConcurrent::run([path, directory, id, version] {
            return removeNativeExtension(path, directory, id, version);
        }));
    };
    QVector<QProcess*> hosts;
    for (auto* host : findChildren<QProcess*>(QString(), Qt::FindDirectChildrenOnly))
        if (host->property("extensionLibrary").toString() == path &&
            host->state() != QProcess::NotRunning)
            hosts.append(host);
    if (hosts.isEmpty())
        QTimer::singleShot(0, this, remove);
    else {
        // Disable may have retired an earlier host which still holds the DLL.
        const auto remaining = std::make_shared<int>(hosts.size());
        for (auto* host : hosts)
            connect(
                host, &QProcess::finished, this,
                [remaining, remove] {
                    if (--*remaining == 0)
                        remove();
                },
                Qt::QueuedConnection);
    }
    emit changed();
    return true;
}
void ExtensionManager::rebuildConfigurationContributions() {
    QVector<ConfigurationContribution> contributions;
    for (const auto& entry : entries_) {
        // Static contributions survive an extension-host crash. Only disable
        // or an invalid manifest removes them; activation is not necessary.
        if (entry->info.enabled && validateManifest(entry->manifest).isEmpty())
            contributions.append({entry->info.id, entry->manifest.value("contributes").toObject()});
    }
    configurationRegistry_.replace(contributions);
    configurationService_->setRegistry(configurationRegistry_);
    emit configurationContributionsChanged();
}
void ExtensionManager::setWorkspace(const QString& path) {
    workspace_ = path;
    configurationService_->setWorkspace(path);
    for (const auto& entry : entries_)
        sendHostMessage(*entry, {{"command", "workspace"},
                                 {"path", path},
                                 {"configuration",
                                  wire::encodeConfigurationModel(configurationService_->model())}});
}
void ExtensionManager::activateForLanguage(const QString& languageId) {
    if (languageId.isEmpty())
        return;
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        const auto events = entry->manifest.value("activationEvents").toArray();
        if (events.contains("onLanguage") || events.contains("onLanguage:" + languageId)) {
            if (!activate(*entry))
                emit changed();
        }
    }
}
bool ExtensionManager::activateForChatParticipant(const QString& participantId) {
    if (participantId.isEmpty())
        return false;
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        const auto contributions =
            entry->manifest.value("contributes").toObject().value("chatParticipants").toArray();
        const bool ownsParticipant = std::any_of(
            contributions.cbegin(), contributions.cend(), [&participantId](const auto& value) {
                return value.toObject().value("id").toString() == participantId;
            });
        if (!ownsParticipant)
            continue;
        if (activate(*entry))
            return true;
        emit changed();
        return false;
    }
    return false;
}
QStringList ExtensionManager::commands() const {
    QStringList result;
    for (const auto& view : nativeViews_.contributions())
        result.append(view.id + ".focus");
    if (workbenchCommandIds_)
        result.append(workbenchCommandIds_());
    if (diffCommand_)
        result.append("vscode.diff");
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        result.append(entry->runtimeCommands.values());
        for (const auto& command :
             entry->manifest.value("contributes").toObject().value("commands").toArray())
            result.append(command.toObject().value("command").toString());
    }
    result.removeDuplicates();
    result.sort();
    return result;
}
QVector<ExtensionCommand> ExtensionManager::commandContributions() const {
    QHash<QString, ExtensionCommand> result;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("commands").toArray()) {
            const auto command = value.toObject();
            const auto id = command.value("command").toString();
            result.insert(id, {id, command.value("title").toString(),
                               command.value("category").toString(), entry->info.id});
        }
    }
    auto values = result.values();
    std::sort(values.begin(), values.end(),
              [](const auto& a, const auto& b) { return a.id < b.id; });
    return values;
}
QSet<QString> ExtensionManager::breakpointLanguages() const {
    QSet<QString> result;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("breakpoints").toArray())
            result.insert(value.toObject().value("language").toString());
    }
    return result;
}
QVector<DebuggerContribution> ExtensionManager::debuggerContributions() const {
    QVector<DebuggerContribution> result;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("debuggers").toArray()) {
            const auto debugger = value.toObject();
            result.append({debugger.value("type").toString(), entry->info.id,
                           debugger.value("configurationAttributes").toObject(),
                           debugger.value("label").toString(),
                           debugger.value("initialConfigurations").toArray()});
        }
    }
    return result;
}
QVector<FormatterContribution>
ExtensionManager::formatterContributions(const QString& languageId) const {
    QVector<FormatterContribution> result;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("formatters").toArray()) {
            if (value.toObject().value("language").toString() == languageId)
                result.append({languageId, entry->info.id, entry->info.name});
        }
    }
    return result;
}
QVector<ChatParticipantContribution> ExtensionManager::chatParticipantContributions() const {
    QVector<ChatParticipantContribution> result;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("chatParticipants").toArray()) {
            const auto item = value.toObject();
            result.append({item.value("id").toString(), item.value("name").toString(),
                           item.value("fullName").toString(item.value("name").toString()),
                           item.value("description").toString(), entry->info.id,
                           item.value("isSticky").toBool(), item.value("commands").toArray(),
                           item.value("disambiguation").toArray(),
                           entry->runtimeChatIcons.value(item.value("id").toString()),
                           entry->runtimeChatFollowups.value(item.value("id").toString())});
        }
    }
    std::sort(result.begin(), result.end(),
              [](const auto& left, const auto& right) { return left.id < right.id; });
    return result;
}
quint64 ExtensionManager::requestChat(const QString& sessionId, const QString& participantId,
                                      const QJsonObject& input) {
    if (sessionId.isEmpty() || participantId.isEmpty())
        return 0;
    const auto normalized = wire::normalizeChatRequest(input);
    if (!normalized)
        return 0;
    const QJsonObject& request = *normalized;
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty() || entry->transitioning)
            continue;
        bool ownsParticipant = false;
        for (const auto& value :
             entry->manifest.value("contributes").toObject().value("chatParticipants").toArray())
            ownsParticipant |= value.toObject().value("id").toString() == participantId;
        if (!ownsParticipant)
            continue;
        if (!activate(*entry)) {
            emit changed();
            return 0;
        }
        const auto id = ++nextChatRequestId_;
        if (!chatStreams_.start(id))
            return 0;
        entry->pendingChats.append({id,
                                    sessionId,
                                    participantId,
                                    request,
                                    chatHistories_.value(sessionId).history(participantId),
                                    {}});
        dispatchQueuedChatRequests(*entry);
        QTimer::singleShot(30000, &entry->callbackLifetime, [this, extension = entry.get(), id] {
            for (qsizetype index = 0; index < extension->pendingChats.size(); ++index) {
                if (extension->pendingChats.at(index).id != id)
                    continue;
                extension->pendingChats.removeAt(index);
                chatStreams_.finish(id);
                emit chatRequestFinished(id, {}, "Chat participant did not register.");
                return;
            }
        });
        return id;
    }
    return 0;
}
bool ExtensionManager::cancelChat(quint64 requestId) {
    for (auto& entry : entries_) {
        for (qsizetype index = 0; index < entry->pendingChats.size(); ++index) {
            if (entry->pendingChats.at(index).id != requestId)
                continue;
            entry->pendingChats.removeAt(index);
            chatStreams_.cancel(requestId);
            chatStreams_.finish(requestId);
            emit chatRequestFinished(requestId, {}, "Cancelled.");
            return true;
        }
        if (!entry->inFlightChats.remove(requestId))
            continue;
        chatStreams_.cancel(requestId);
        sendHostMessage(*entry,
                        {{"command", "chatCancel"}, {"requestId", QString::number(requestId)}});
        chatStreams_.finish(requestId);
        emit chatRequestFinished(requestId, {}, "Cancelled.");
        return true;
    }
    return false;
}
bool ExtensionManager::sendChatFeedback(quint64 requestId, ChatResultFeedbackKind kind) {
    if (kind != ChatResultFeedbackKind::Helpful && kind != ChatResultFeedbackKind::Unhelpful)
        return false;
    const auto it = completedChats_.constFind(requestId);
    if (it == completedChats_.cend())
        return false;
    auto* owner = it->owner;
    if (!owner || !owner->info.enabled || !owner->info.active ||
        !owner->runtimeChatParticipants.contains(it->participantId))
        return false;
    sendHostMessage(*owner, {{"command", "chatFeedback"},
                             {"participantId", it->participantId},
                             {"result", it->result},
                             {"kind", static_cast<int>(kind)}});
    return true;
}
void ExtensionManager::closeChatSession(const QString& sessionId) {
    QVector<quint64> requests;
    for (const auto& entry : entries_) {
        for (const auto& request : entry->pendingChats)
            if (request.sessionId == sessionId)
                requests.append(request.id);
        for (auto it = entry->inFlightChats.cbegin(); it != entry->inFlightChats.cend(); ++it)
            if (it.value().sessionId == sessionId)
                requests.append(it.key());
    }
    for (quint64 id : requests)
        cancelChat(id);
    chatHistories_.remove(sessionId);
    for (auto it = completedChats_.begin(); it != completedChats_.end();) {
        if (it.value().sessionId != sessionId) {
            ++it;
            continue;
        }
        if (it.value().owner && it.value().owner->info.active)
            sendHostMessage(*it.value().owner, {{"command", "chatFollowupCancel"},
                                                {"requestId", QString::number(it.key())}});
        it = completedChats_.erase(it);
    }
}
QJsonArray ExtensionManager::chatHistory(const QString& sessionId,
                                         const QString& participantId) const {
    return chatHistories_.value(sessionId).history(participantId);
}
std::optional<ExtensionInfo> ExtensionManager::uriTarget(const QUrl& uri) const {
    for (const auto& entry : entries_) {
        if (entry->info.enabled && entry->info.error.isEmpty() && !entry->stopping &&
            wire::isExtensionUri(uri, entry->info.id))
            return entry->info;
    }
    return {};
}
bool ExtensionManager::dispatchUri(const QUrl& uri) {
    const auto target = uriTarget(uri);
    return target && uriHandlers_->dispatch(target->id, uri);
}
bool ExtensionManager::executeCommand(const QString& id) { return requestCommand(id) != 0; }
quint64 ExtensionManager::requestCommand(const QString& id, const QJsonArray& arguments,
                                         const QString& owner) {
    for (const auto& view : nativeViews_.contributions())
        if (id == view.id + ".focus") {
            const auto requestId = ++nextCommandRequestId_;
            QTimer::singleShot(0, this, [this, requestId, viewId = view.id, arguments] {
                const auto error = !arguments.isEmpty() ? QString("View focus takes no arguments.")
                                   : nativeViews_.reveal(viewId) ? QString{}
                                                                 : QString("View is unavailable.");
                emit commandResult(requestId, QJsonValue{}, error);
            });
            return requestId;
        }
    if (id == "vscode.diff" && diffCommand_) {
        const auto requestId = ++nextCommandRequestId_;
        // Defer completion until the forwarding caller has recorded its request.
        QTimer::singleShot(0, this, [this, requestId, owner, arguments] {
            try {
                const auto future = diffCommand_(owner, arguments);
                auto* watcher = new QFutureWatcher<QJsonValue>(this);
                connect(watcher, &QFutureWatcher<QJsonValue>::finished, this,
                        [this, watcher, requestId] {
                            watcher->deleteLater();
                            try {
                                if (watcher->future().isCanceled())
                                    throw std::runtime_error("Diff opening was canceled.");
                                emit commandResult(requestId, watcher->result(), {});
                            } catch (const std::exception& error) {
                                emit commandResult(requestId, {}, QString::fromUtf8(error.what()));
                            } catch (...) {
                                emit commandResult(requestId, {}, "Could not open diff editor.");
                            }
                        });
                watcher->setFuture(future);
            } catch (const std::exception& error) {
                emit commandResult(requestId, {}, QString::fromUtf8(error.what()));
            }
        });
        return requestId;
    }
    if (workbenchCommandIds_ && workbenchCommandIds_().contains(id)) {
        const auto requestId = ++nextCommandRequestId_;
        QTimer::singleShot(0, this, [this, requestId, id, arguments] {
            QString error;
            try {
                if (!arguments.isEmpty())
                    error = "Workbench command arguments are not supported yet.";
                else if (!invokeWorkbenchCommand_ || !invokeWorkbenchCommand_(id))
                    error = "Workbench command is unavailable.";
            } catch (const std::exception& exception) {
                error = QString::fromUtf8(exception.what());
            } catch (...) {
                error = "Workbench command failed.";
            }
            emit commandResult(requestId, QJsonValue(QJsonValue::Undefined), error);
        });
        return requestId;
    }
    for (auto& entry : entries_) {
        if (!entry->info.enabled || !entry->info.error.isEmpty())
            continue;
        if (entry->transitioning)
            continue;
        bool contributed = false;
        for (const auto& command :
             entry->manifest.value("contributes").toObject().value("commands").toArray())
            contributed |= command.toObject().value("command").toString() == id;
        if (contributed || entry->runtimeCommands.contains(id) ||
            entry->manifest.value("activationEvents").toArray().contains("onCommand:" + id)) {
            if (!activate(*entry)) {
                emit changed();
                return 0;
            }
            const quint64 requestId = ++nextCommandRequestId_;
            if (entry->info.active) {
                entry->inFlightCommands.insert(requestId);
                sendHostMessage(*entry, {{"command", "execute"},
                                         {"id", id},
                                         {"requestId", QString::number(requestId)},
                                         {"arguments", arguments}});
            } else
                entry->pendingCommands.append({requestId, id, arguments});
            QTimer::singleShot(
                30000, &entry->callbackLifetime, [this, extension = entry.get(), requestId] {
                    const auto pending = std::remove_if(
                        extension->pendingCommands.begin(), extension->pendingCommands.end(),
                        [requestId](const auto& request) { return request.id == requestId; });
                    if (pending != extension->pendingCommands.end()) {
                        extension->pendingCommands.erase(pending, extension->pendingCommands.end());
                        emit commandResult(requestId, {}, "Command timed out.");
                    } else if (extension->inFlightCommands.remove(requestId)) {
                        emit commandResult(requestId, {}, "Command timed out.");
                    }
                });
            return requestId;
        }
    }
    return 0;
}
quint64 ExtensionManager::requestDocumentFormat(const QString& languageId, const QString& source,
                                                const QString& extensionId) {
    // Bound IPC cost and avoid sending large-file-mode documents to plugins.
    if (source.toUtf8().size() > 2 * 1024 * 1024)
        return 0;
    const auto providers = formatterContributions(languageId);
    if (providers.isEmpty() || (providers.size() != 1 && extensionId.isEmpty()))
        return 0;
    const QString selectedId = extensionId.isEmpty() ? providers.first().extensionId : extensionId;
    if (std::none_of(providers.begin(), providers.end(), [&selectedId](const auto& provider) {
            return provider.extensionId == selectedId;
        }))
        return 0;
    for (auto& entry : entries_) {
        if (entry->info.id != selectedId || entry->transitioning)
            continue;
        if (!activate(*entry)) {
            emit changed();
            return 0;
        }
        const auto id = ++nextFormatRequestId_;
        if (entry->info.active) {
            if (!entry->runtimeFormatters.contains(languageId))
                return 0;
            entry->inFlightFormats.insert(id);
            sendHostMessage(*entry, {{"command", "format"},
                                     {"requestId", QString::number(id)},
                                     {"language", languageId},
                                     {"text", source}});
        } else {
            entry->pendingFormats.append({id, languageId, source});
        }
        QTimer::singleShot(30000, &entry->callbackLifetime, [this, extension = entry.get(), id] {
            const auto pending =
                std::remove_if(extension->pendingFormats.begin(), extension->pendingFormats.end(),
                               [id](const auto& request) { return request.id == id; });
            if (pending != extension->pendingFormats.end()) {
                extension->pendingFormats.erase(pending, extension->pendingFormats.end());
                emit documentFormatResult(id, {}, "Formatting timed out.");
            } else if (extension->inFlightFormats.remove(id)) {
                emit documentFormatResult(id, {}, "Formatting timed out.");
            }
        });
        return id;
    }
    return 0;
}
quint64 ExtensionManager::requestDebugAdapter(const QString& type, const QJsonObject& configuration,
                                              const QString& sessionId,
                                              const QJsonValue& folderOverride) {
    // A debug type must have exactly one enabled owner. Ambiguous ownership must
    // not silently select an arbitrary adapter.
    QString ownerId;
    for (const auto& debugger : debuggerContributions()) {
        if (debugger.type != type)
            continue;
        if (!ownerId.isEmpty())
            return 0;
        ownerId = debugger.extensionId;
    }
    if (ownerId.isEmpty())
        return 0;
    auto resolved = configuration;
    resolved.insert("type", type);
    QJsonObject session{
        {"id",
         sessionId.isEmpty() ? QUuid::createUuid().toString(QUuid::WithoutBraces) : sessionId},
        {"type", type},
        {"name", configuration.value("name").toString(type)},
        {"configuration", resolved},
        {"workspaceFolder",
         workspace_.isEmpty()
             ? QJsonValue(QJsonValue::Null)
             : QJsonValue(QJsonObject{
                   {"uri", QUrl::fromLocalFile(workspace_).toString(QUrl::FullyEncoded)},
                   {"name", QFileInfo(workspace_).fileName()},
                   {"index", 0}})}};
    if (folderOverride.isNull() || folderOverride.isObject())
        session.insert("workspaceFolder", folderOverride);
    for (auto& entry : entries_) {
        if (entry->info.id != ownerId || entry->transitioning)
            continue;
        if (!activate(*entry)) {
            emit changed();
            return 0;
        }
        const auto id = ++nextDebugAdapterRequestId_;
        if (entry->info.active) {
            sendDebugAdapterRequest(*entry, id, type, session);
        } else {
            entry->pendingDebugRequests.append({id, type, session});
        }
        QTimer::singleShot(10000, &entry->callbackLifetime, [this, extension = entry.get(), id] {
            extension->debugRequestContexts.remove(id);
            const auto pending = std::remove_if(
                extension->pendingDebugRequests.begin(), extension->pendingDebugRequests.end(),
                [id](const auto& request) { return request.id == id; });
            if (pending != extension->pendingDebugRequests.end()) {
                extension->pendingDebugRequests.erase(pending,
                                                      extension->pendingDebugRequests.end());
                emit debugAdapterResult(id, {}, "Debug adapter resolution timed out.");
            } else if (extension->inFlightDebugRequests.remove(id)) {
                sendHostMessage(*extension, {{"command", "releaseDebugAdapter"},
                                             {"requestId", QString::number(id)}});
                emit debugAdapterResult(id, {}, "Debug adapter resolution timed out.");
            }
        });
        return id;
    }
    return 0;
}
bool ExtensionManager::releaseDebugAdapter(quint64 id) {
    if (!id)
        return false;
    const bool trackers = debugTrackers_->release(id);
    for (auto& entry : entries_) {
        entry->debugRequestContexts.remove(id);
        const auto found =
            std::find_if(entry->pendingDebugRequests.begin(), entry->pendingDebugRequests.end(),
                         [id](const auto& request) { return request.id == id; });
        if (found != entry->pendingDebugRequests.end()) {
            entry->pendingDebugRequests.erase(found);
            emit debugAdapterResult(id, {}, "Debug adapter resolution was canceled.");
            return true;
        }
        const bool pending = entry->inFlightDebugRequests.remove(id);
        const bool active = entry->activeInlineAdapters.remove(id);
        if (!pending && !active)
            continue;
        sendHostMessage(*entry,
                        {{"command", "releaseDebugAdapter"}, {"requestId", QString::number(id)}});
        if (pending)
            emit debugAdapterResult(id, {}, "Debug adapter resolution was canceled.");
        return true;
    }
    return trackers;
}
quint64 ExtensionManager::requestDebugConfiguration(const QString& type, const QString& method,
                                                    const QJsonValue& configuration,
                                                    DebugConfigurationProviderTriggerKind trigger,
                                                    const QJsonValue& folderOverride) {
    QStringList awaiting;
    for (const auto& entry : entries_) {
        if (!entry->info.enabled || entry->transitioning)
            continue;
        bool matches = false;
        for (const auto& item :
             entry->manifest.value("contributes").toObject().value("debuggers").toArray())
            if (item.toObject().value("type") == QJsonValue(type)) {
                matches = true;
                break;
            }
        const auto events = entry->manifest.value("activationEvents").toArray();
        const auto activation = method == "provideDebugConfigurations"
                                    ? (trigger == DebugConfigurationProviderTriggerKind::Dynamic
                                           ? QString("onDebugDynamicConfigurations")
                                           : QString("onDebugInitialConfigurations"))
                                    : QString("onDebugResolve:") + type;
        if (events.contains(activation) || events.contains("onDebug"))
            matches = true;
        if (!matches)
            continue;
        if (!activate(*entry))
            return 0;
        awaiting.append(entry->info.id);
    }
    std::optional<WorkspaceFolder> folder;
    if (!workspace_.isEmpty())
        folder =
            WorkspaceFolder{QUrl::fromLocalFile(workspace_), QFileInfo(workspace_).fileName(), 0};
    if (folderOverride.isNull())
        folder.reset();
    else if (folderOverride.isObject()) {
        const auto value = folderOverride.toObject();
        folder = WorkspaceFolder{QUrl(value.value("uri").toString()),
                                 value.value("name").toString(), value.value("index").toInt()};
    }
    return debugConfigurations_->request(type, method, configuration, folder, awaiting, trigger);
}
bool ExtensionManager::cancelDebugConfiguration(quint64 id) {
    return debugConfigurations_->cancel(id);
}
void ExtensionManager::setDebugSessionHandler(WorkbenchDebugSessions::Handler handler) {
    debugSessions_->setHandler(std::move(handler));
}
void ExtensionManager::setDebugStartHandler(WorkbenchDebugStarts::Handler handler) {
    debugStarts_->setHandler(std::move(handler));
}
void ExtensionManager::publishDebugTrackerEvent(const QString& id, const QJsonObject& event) {
    debugTrackers_->publish(id, event);
}
void ExtensionManager::publishDebugSessionEvent(const QJsonObject& event) {
    QJsonObject message = event;
    message.insert("command", "debugSessionEvent");
    if (!debugSessions_->publish(message))
        return;
    for (const auto& entry : entries_)
        if (entry->authenticated && entry->info.active && !entry->stopping)
            sendHostMessage(*entry, message);
}
} // namespace litecode::extensions
