#pragma once
#include <QJsonObject>
#include <QJsonValue>
#include <QProcessEnvironment>
#include <QVector>

namespace litecode::debug {
struct DebugConfiguration {
    QString name, type, request;
    QJsonObject values;
};
struct VariableContext {
    QString workspaceFolder;
    QString file;
    QString selectedText;
    int lineNumber{1};
    QProcessEnvironment environment{QProcessEnvironment::systemEnvironment()};
};
class DebugConfigurations final {
  public:
    static QString filePath(const QString& workspaceFolder);
    static QVector<DebugConfiguration> load(const QString& workspaceFolder, QString* error);
    static QVector<DebugConfiguration> parse(const QByteArray& jsonc, QString* error,
                                             const QString& platform = {});
    static QJsonValue resolve(const QJsonValue& value, const VariableContext& context,
                              QString* error);
    static bool validateAgainstSchema(const QJsonObject& configuration,
                                      const QJsonObject& schema, QString* error);
    static QJsonObject parseJsonc(const QByteArray& bytes, QString* error);
};
} // namespace litecode::debug
