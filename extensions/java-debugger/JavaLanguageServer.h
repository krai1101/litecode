#pragma once
#include "debug/DapProtocol.h"
#include "extensions/ExtensionApi.h"
#include <QHash>
#include <QProcess>
#include <QPromise>
#include <QTemporaryDir>
#include <QTimer>

namespace litecode::java {
class JavaLanguageServer final : public QObject {
  public:
    explicit JavaLanguageServer(extensions::ExtensionContext& context);
    ~JavaLanguageServer() override;
    QFuture<QJsonValue> execute(const QString& workspace, const QString& command,
                                QJsonArray arguments = {});
    QFuture<QJsonValue> buildSourceProject(const QString& workspace, const QUrl& source);

  private:
    QFuture<QJsonValue> ensureStarted(const QString& workspace);
    QFuture<QJsonValue> request(const QString& method, QJsonObject params);
    void send(const QJsonObject& message);
    void receive(const QJsonObject& message);
    void reset(const QString& error);

    extensions::ExtensionContext& context_;
    std::shared_ptr<extensions::LogOutputChannel> log_;
    QProcess process_;
    debug::DapDecoder decoder_;
    QHash<int, std::shared_ptr<QPromise<QJsonValue>>> pending_;
    QFuture<QJsonValue> ready_;
    std::shared_ptr<QPromise<QJsonValue>> startup_;
    QTimer startupTimeout_;
    std::unique_ptr<QTemporaryDir> data_;
    QString workspace_;
    int sequence_{};
};
} // namespace litecode::java
