#pragma once
#include "extensions/EnvironmentInfo.h"
#include "extensions/HostClipboard.h"
#include "extensions/HostExternalUris.h"
#include <QMutex>
#include <QMutexLocker>
#include <utility>

namespace litecode::extensions {
class HostEnvironment final : public Environment {
  public:
    HostEnvironment(EnvironmentInfo info, std::function<void(const QJsonObject&)> send)
        : info_(std::move(info)), clipboard_(send), externalUris_(std::move(send)), shell_(info_.shell), logLevel_(info_.logLevel) {}
    ~HostEnvironment() override { shutdown(); }
    Clipboard& clipboard() override { return clipboard_; }
    HostClipboard& hostClipboard() { return clipboard_; }
    HostExternalUris& externalUris() { return externalUris_; }
    QString uriScheme() const override { return info_.uriScheme; }
    QFuture<bool> openExternal(const QUrl& target) override { return externalUris_.openExternal(target); }
    QFuture<QUrl> asExternalUri(const QUrl& target) override { return externalUris_.asExternalUri(target); }
    QString appName() const override { return info_.appName; }
    QString appRoot() const override { return info_.appRoot; }
    QString appHost() const override { return info_.appHost; }
    QString language() const override { return info_.language; }
    QString machineId() const override { return info_.machineId; }
    QString sessionId() const override { return info_.sessionId; }
    bool isNewAppInstall() const override { return litecode::extensions::isNewAppInstall(info_.firstSessionDate, QDateTime::currentDateTimeUtc()); }
    bool isAppPortable() const override { return info_.isAppPortable; }
    std::optional<QString> remoteName() const override { return info_.remoteName; }
    UIKind uiKind() const override { return info_.uiKind; }
    QString shell() const override { QMutexLocker lock(&stateMutex_); return shell_; }
    Event<QString> onDidChangeShell() const override { return shellChanged_.event(); }
    LogLevel logLevel() const override { QMutexLocker lock(&stateMutex_); return logLevel_; }
    Event<LogLevel> onDidChangeLogLevel() const override { return logLevelChanged_.event(); }
    void acceptLogLevel(LogLevel level) {
        if (static_cast<int>(level) < 0 || static_cast<int>(level) > 5) return;
        {
            QMutexLocker lock(&stateMutex_);
            if (stopped_ || logLevel_ == level) return;
            logLevel_ = level;
        }
        logLevelChanged_.fire(level);
    }
    void acceptShell(QString value) {
        {
            QMutexLocker lock(&stateMutex_);
            if (stopped_ || shell_ == value) return;
            shell_ = value;
        }
        shellChanged_.fire(value);
    }
    void shutdown() {
        { QMutexLocker lock(&stateMutex_); stopped_ = true; }
        shellChanged_.dispose();
        logLevelChanged_.dispose();
        clipboard_.shutdown();
        externalUris_.shutdown();
    }
  private:
    const EnvironmentInfo info_;
    HostClipboard clipboard_;
    HostExternalUris externalUris_;
    mutable QMutex stateMutex_;
    QString shell_;
    EventEmitter<QString> shellChanged_;
    LogLevel logLevel_;
    EventEmitter<LogLevel> logLevelChanged_;
    bool stopped_{};
};
} // namespace litecode::extensions
