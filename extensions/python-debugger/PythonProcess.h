#pragma once
#include <QFuture>
#include <QObject>
#include <QStringList>
#include <extensions/CancellationApi.h>
namespace litecode::python {
struct ProcessResult {
    int exitCode{-1};
    QByteArray output, error;
};
QFuture<ProcessResult> runPythonProcess(QObject& owner, const QString& command,
                                        const QStringList& args, const QString& cwd = {},
                                        extensions::CancellationToken token = {});
} // namespace litecode::python
