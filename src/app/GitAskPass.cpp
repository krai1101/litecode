#include <QCoreApplication>
#include <QElapsedTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocalSocket>
#include <cstdio>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

// No widgets, logging, credential storage or account-specific dependencies.
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        return 1;
#endif
    if (argc < 2)
        return 1;
    const auto endpoint = qEnvironmentVariable("LITECODE_GIT_ASKPASS_ENDPOINT");
    const auto token = qEnvironmentVariable("LITECODE_GIT_ASKPASS_TOKEN");
    const auto operation = qEnvironmentVariable("LITECODE_GIT_ASKPASS_OPERATION");
    if (endpoint.isEmpty() || token.isEmpty() || operation.isEmpty())
        return 1;
    const auto prompt = app.arguments().mid(1).join(' ');
    if (prompt.size() > 8192)
        return 1;
    QLocalSocket socket;
    socket.connectToServer(endpoint);
    if (!socket.waitForConnected(5000))
        return 1;
    socket.write(
        QJsonDocument(QJsonObject{{"token", token}, {"operation", operation}, {"prompt", prompt}})
            .toJson(QJsonDocument::Compact) +
        '\n');
    if (!socket.waitForBytesWritten(5000))
        return 1;
    QByteArray bytes;
    QElapsedTimer deadline;
    deadline.start();
    while (!bytes.contains('\n')) {
        if (!socket.waitForReadyRead(qMax(1, 240000 - int(deadline.elapsed()))))
            return 1;
        bytes += socket.readAll();
        if (bytes.size() > 65536 || deadline.elapsed() > 240000)
            return 1;
    }
    const auto response = QJsonDocument::fromJson(bytes).object();
    if (!response.value("accepted").toBool())
        return 1;
    const auto value = response.value("value").toString().toUtf8();
    if (value.contains('\n') || value.contains('\r') || value.contains('\0'))
        return 1;
    return std::fwrite(value.constData(), 1, size_t(value.size()), stdout) ==
                       size_t(value.size()) &&
                   std::fputc('\n', stdout) != EOF
               ? 0
               : 1;
}
