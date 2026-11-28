#pragma once
#include <QJsonArray>
#include <QJsonObject>
namespace litecode::python {
QVector<QJsonObject> pythonConfigurations(const QString& workspace, bool initial);
QJsonObject preparePythonConfiguration(QJsonObject configuration, const QString& workspace,
                                       const QString& activeFile, bool justMyCode,
                                       const QString& envFile);
bool pythonRemoteConnect(const QJsonObject& configuration);
void validatePythonConfiguration(const QJsonObject& configuration);
} // namespace litecode::python
