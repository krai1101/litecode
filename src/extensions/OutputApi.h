#pragma once
#include "extensions/PrimitivesApi.h"
#include "extensions/LogApi.h"
#include <QJsonArray>
#include <QString>
#include <memory>
#include <optional>
#include <exception>

namespace litecode::extensions {
template<typename T> class Event;
enum class ViewColumn { Active = -1, Beside = -2, One = 1, Two, Three, Four, Five, Six, Seven, Eight, Nine };
class OutputChannel {
  public:
    virtual ~OutputChannel() = default;
    [[nodiscard]] virtual QString name() const = 0;
    virtual void append(const QString& value) = 0;
    virtual void appendLine(const QString& value) = 0;
    virtual void replace(const QString& value) = 0;
    virtual void clear() = 0;
    virtual void show(bool preserveFocus = false) = 0;
    void show(std::optional<ViewColumn>, bool preserveFocus = false) { show(preserveFocus); }
    virtual void hide() = 0;
    virtual void dispose() = 0;
};
class LogOutputChannel : public OutputChannel {
  public:
    [[nodiscard]] virtual LogLevel logLevel() const = 0;
    [[nodiscard]] virtual Event<LogLevel> onDidChangeLogLevel() const = 0;
    virtual void trace(const QString& message, const QJsonArray& args = {}) = 0;
    virtual void debug(const QString& message, const QJsonArray& args = {}) = 0;
    virtual void info(const QString& message, const QJsonArray& args = {}) = 0;
    virtual void warn(const QString& message, const QJsonArray& args = {}) = 0;
    virtual void error(const QString& message, const QJsonArray& args = {}) = 0;
    void error(const std::exception& error, const QJsonArray& args = {}) { this->error(QString::fromUtf8(error.what()), args); }
};
// Native overload tag for createOutputChannel's { log: true } option.
struct LogOutputChannelOptions final { const bool log{true}; };
} // namespace litecode::extensions
