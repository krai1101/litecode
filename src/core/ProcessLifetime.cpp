#include "core/ProcessLifetime.h"

#include <QCoreApplication>
#include <QProcess>
#include <QTimer>

namespace litecode::core {

void retireProcess(QProcess*& process) {
    if (process == nullptr)
        return;
    QProcess* retiring = process;
    process = nullptr;
    QObject::disconnect(retiring, nullptr, nullptr, nullptr);
    if (retiring->state() == QProcess::NotRunning) {
        retiring->deleteLater();
        return;
    }

    if (QCoreApplication::instance() != nullptr) {
        retiring->setParent(QCoreApplication::instance());
    }
    QObject::connect(retiring, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), retiring,
                     &QObject::deleteLater);
    retiring->terminate();
    QTimer::singleShot(250, retiring, [retiring] {
        if (retiring->state() != QProcess::NotRunning) {
            retiring->kill();
        }
    });
}

} // namespace litecode::core
