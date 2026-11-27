#pragma once
#include "extensions/OutputApi.h"
#include <QHash>
#include <QJsonObject>
#include <QObject>
#include <QVector>

namespace litecode::extensions {
struct OutputChannelInfo final {
    QString id, owner, name, languageId, text;
    bool log{};
    LogLevel level{LogLevel::Info};
};
class WorkbenchOutput final : public QObject {
    Q_OBJECT
  public:
    using QObject::QObject;
    bool accept(const QString& owner, const QJsonObject& message);
    void removeOwner(const QString& owner);
    QVector<OutputChannelInfo> channels() const;
    std::optional<OutputChannelInfo> channel(const QString& id) const;
    void setLogLevel(const QString& id, LogLevel level);
    void setGlobalLogLevel(LogLevel level);
    [[nodiscard]] LogLevel globalLogLevel() const { return globalLevel_; }
    void clear(const QString& id);
  signals:
    void channelsChanged();
    void textChanged(const QString& id, const QString& text, bool append);
    void revealRequested(const QString& id, bool preserveFocus);
    void hideRequested(const QString& id);
    void channelError(const QString& message);
    void logLevelRequested(const QString& owner, const QString& id, int level);
    void globalLogLevelChanged(int level);
    void clearRequested(const QString& owner, const QString& id);

  private:
    QHash<QString, OutputChannelInfo> channels_;
    QHash<QString, LogLevel> levelOverrides_;
    LogLevel globalLevel_{LogLevel::Info};
};
} // namespace litecode::extensions
