#pragma once
#include "extensions/ExtensionApi.h"
#include <QJsonObject>
#include <QObject>
#include <functional>

namespace litecode::extensions {
struct OutputRuntime;
class HostOutput final : public QObject {
  public:
    HostOutput(QString logs, std::function<void(const QJsonObject&)> send);
    ~HostOutput() override;
    std::shared_ptr<OutputChannel>
    createOutputChannel(const QString& name, const std::optional<QString>& languageId = {});
    std::shared_ptr<LogOutputChannel> createOutputChannel(const QString& name,
                                                          LogOutputChannelOptions);
    void setLogLevel(const QString& id, LogLevel level);
    void setGlobalLogLevel(LogLevel level);
    [[nodiscard]] LogLevel globalLogLevel() const;
    void clear(const QString& id);
    void shutdown();

  private:
    std::shared_ptr<LogOutputChannel> create(const QString& name,
                                             const std::optional<QString>& language, bool log);
    std::shared_ptr<OutputRuntime> runtime_;
};
} // namespace litecode::extensions
