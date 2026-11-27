#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/LogApi.h"
#include <QFuture>
#include <QString>
#include <optional>
#include <QUrl>

namespace litecode::extensions {
enum class UIKind { Desktop = 1, Web = 2 };
class Clipboard {
  public:
    virtual ~Clipboard() = default;
    virtual QFuture<QString> readText() = 0;
    virtual QFuture<void> writeText(const QString& value) = 0;
};
// Native service equivalent of vscode.env. Additional stable environment
// members are still being implemented; this is not environment API parity.
class Environment {
  public:
    virtual ~Environment() = default;
    virtual Clipboard& clipboard() = 0;
    [[nodiscard]] virtual QString uriScheme() const = 0;
    virtual QFuture<bool> openExternal(const QUrl& target) = 0;
    virtual QFuture<QUrl> asExternalUri(const QUrl& target) = 0;
    [[nodiscard]] virtual QString appName() const = 0;
    [[nodiscard]] virtual QString appRoot() const = 0;
    [[nodiscard]] virtual QString appHost() const = 0;
    [[nodiscard]] virtual QString language() const = 0;
    [[nodiscard]] virtual QString machineId() const = 0;
    [[nodiscard]] virtual QString sessionId() const = 0;
    [[nodiscard]] virtual bool isNewAppInstall() const = 0;
    [[nodiscard]] virtual bool isAppPortable() const = 0;
    [[nodiscard]] virtual std::optional<QString> remoteName() const = 0;
    [[nodiscard]] virtual UIKind uiKind() const = 0;
    [[nodiscard]] virtual QString shell() const = 0;
    [[nodiscard]] virtual Event<QString> onDidChangeShell() const = 0;
    [[nodiscard]] virtual LogLevel logLevel() const = 0;
    [[nodiscard]] virtual Event<LogLevel> onDidChangeLogLevel() const = 0;
};
} // namespace litecode::extensions
