#include "PythonProcess.h"
#include <QPointer>
#include <QProcess>
#include <QPromise>
#include <QTimer>
#include <memory>
namespace litecode::python {
namespace {
class ProcessTask final : public QObject {
  public:
    ProcessTask(QObject& owner) : QObject(&owner) { promise_.start(); }
    ~ProcessTask() override {
        process_.disconnect(this);
        timer_.stop();
        cancellation_.dispose();
        if (process_.state() != QProcess::NotRunning)
            process_.kill();
        if (!finished_) {
            promise_.addResult({-1, {}, "Python discovery cancelled."});
            promise_.finish();
        }
    }
    QFuture<ProcessResult> start(const QString& command, const QStringList& args,
                                 const QString& cwd, extensions::CancellationToken token) {
        const auto future = promise_.future();
        timer_.setSingleShot(true);
        connect(&timer_, &QTimer::timeout, this, [this] { process_.kill(); });
        connect(&process_, &QProcess::readyReadStandardOutput, this, [this] {
            output_ += process_.readAllStandardOutput();
            if (output_.size() > 1024 * 1024)
                process_.kill();
        });
        connect(&process_, &QProcess::readyReadStandardError, this, [this] {
            error_ += process_.readAllStandardError();
            if (error_.size() > 65536)
                process_.kill();
        });
        connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
            if (error == QProcess::FailedToStart)
                finish(-1);
        });
        connect(&process_, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
                [this](int code, QProcess::ExitStatus status) {
                    finish(status == QProcess::NormalExit ? code : -1);
                });
        cancellation_ = token.onCancellationRequested([self = QPointer<ProcessTask>(this)] {
            if (self)
                QMetaObject::invokeMethod(
                    self,
                    [self] {
                        if (self)
                            self->process_.kill();
                    },
                    Qt::QueuedConnection);
        });
        if (token.isCancellationRequested()) {
            finish(-1);
            return future;
        }
#ifdef Q_OS_WIN
        process_.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* arguments) {
            arguments->flags |= 0x08000000; // CREATE_NO_WINDOW
        });
#endif
        if (!cwd.isEmpty())
            process_.setWorkingDirectory(cwd);
        process_.start(command, args, QIODevice::ReadOnly);
        timer_.start(5000); // Bounded interpreter probes; never a UI loading timeout.
        return future;
    }

  private:
    void finish(int code) {
        if (finished_)
            return;
        finished_ = true;
        timer_.stop();
        cancellation_.dispose();
        output_ += process_.readAllStandardOutput();
        error_ += process_.readAllStandardError();
        if (error_.isEmpty() && code < 0)
            error_ = process_.errorString().toUtf8();
        promise_.addResult({code, output_.left(1024 * 1024), error_.left(65536)});
        promise_.finish();
        deleteLater();
    }
    QProcess process_;
    QTimer timer_;
    QPromise<ProcessResult> promise_;
    extensions::Disposable cancellation_;
    QByteArray output_, error_;
    bool finished_{};
};
} // namespace
QFuture<ProcessResult> runPythonProcess(QObject& owner, const QString& command,
                                        const QStringList& args, const QString& cwd,
                                        extensions::CancellationToken token) {
    return (new ProcessTask(owner))->start(command, args, cwd, token);
}
} // namespace litecode::python
