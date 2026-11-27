#pragma once
#include <QJsonObject>
#include <QProcessEnvironment>
namespace litecode::python {
QJsonObject
pythonEnvironment(const QString& file, const QJsonObject& overrides,
                  const QProcessEnvironment& inherited = QProcessEnvironment::systemEnvironment());
}
