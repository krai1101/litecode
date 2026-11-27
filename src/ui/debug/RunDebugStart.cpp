#include "ui/RunDebugPanel.h"
#include <QFileInfo>
#include <QPointer>
#include <QUrl>
#include <stdexcept>
#include <utility>

namespace litecode::ui {
QFuture<bool> RunDebugPanel::startFromExtension(const QJsonValue& folder, const QJsonValue& value,
    const QJsonObject& options, extensions::CancellationToken cancellation) {
    auto promise = std::make_shared<QPromise<bool>>(); promise->start(); auto result = promise->future();
    const auto reject = [&promise](const QString& error) {
        promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString()))); promise->finish();
    };
    if (!canStart() || debugStartResult_) { reject("Concurrent debug sessions are not implemented."); return result; }
    if (options.contains("parentSession")) { reject("Parent/child debug sessions are not implemented."); return result; }
    if (cancellation.isCancellationRequested()) { promise->addResult(false); promise->finish(); return result; }
    const auto path = folder.isUndefined() ? QString() : QUrl(folder.toObject().value("uri").toString()).toLocalFile();
    if (!folder.isUndefined() && !QFileInfo(path).isDir()) { reject("Debug workspace folder does not exist."); return result; }
    QJsonObject configuration;
    if (value.isString()) {
        if (path.isEmpty()) { reject("launch.json does not exist for the passed workspace folder."); return result; }
        QString error; const auto configurations = debug::DebugConfigurations::load(path, &error);
        if (!error.isEmpty()) { reject(error); return result; }
        for (const auto& item : configurations) if (item.name == value.toString()) { configuration = item.values; break; }
        if (configuration.isEmpty()) { reject("Named launch or compound configuration is unavailable: " + value.toString()); return result; }
    } else if (value.isObject()) configuration = value.toObject();
    else { reject("Invalid debug configuration."); return result; }
    if (options.value("noDebug").toBool()) configuration.insert("noDebug", true);
    debugStartResult_ = promise; debugStartOptions_ = options;
    launchFolder_ = folder.isUndefined() ? QJsonValue(QJsonValue::Null) : folder;
    launchVariables_ = variableContextSource_ ? variableContextSource_() : debug::VariableContext{};
    launchVariables_.workspaceFolder = path;
    const QPointer<RunDebugPanel> self(this);
    debugStartCancellation_ = cancellation.onCancellationRequested([self, weak = std::weak_ptr<QPromise<bool>>(promise)] {
        if (!self) return;
        QMetaObject::invokeMethod(self, [self, weak] {
            const auto expected = weak.lock();
            if (self && expected && self->debugStartResult_ == expected) self->stop();
        }, Qt::QueuedConnection);
    });
    requestConfigurationStep("resolveDebugConfiguration", configuration);
    return result;
}
void RunDebugPanel::finishDebugStart(bool success, const QString& error) {
    const auto promise = std::exchange(debugStartResult_, {});
    if (!promise) return;
    debugStartCancellation_.dispose();
    if (!error.isEmpty()) promise->setException(std::make_exception_ptr(std::runtime_error(error.toStdString())));
    else promise->addResult(success);
    promise->finish();
}
} // namespace litecode::ui
