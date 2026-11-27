#include "debug/DapProtocol.h"
#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <cstdio>
#include <cstdlib>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

// Protocol fixture only, not a debugger. Deliberately does not create windows.
int main(int argc, char** argv) {
    QCoreApplication application(argc, argv);
    if (application.arguments().contains("--terminal-target")) {
        std::printf("TERMINAL_READY\nARG:%s\nENV:%s\nCWD:%s\n",
                    application.arguments().last().toUtf8().constData(),
                    qgetenv("LITECODE_DEBUG_TEST").constData(),
                    QDir::currentPath().toUtf8().constData());
        std::fflush(stdout);
        char input[256]{};
        if (!std::fgets(input, sizeof(input), stdin))
            return 2;
        const auto text = QByteArray(input).trimmed();
        std::printf("TERMINAL_INPUT:%s\n", text.constData());
        std::fflush(stdout);
        return 0;
    }
#ifdef Q_OS_WIN
    // CRT newline translation would corrupt byte-counted DAP frames.
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    litecode::debug::DapDecoder decoder;
    int sequence = 0;
    const bool session = application.arguments().contains("--session");
    QJsonObject pendingLaunch;
    const auto write = [&](QJsonObject message) {
        message.insert("seq", ++sequence);
        const auto frame = litecode::debug::DapDecoder::encode(message);
        std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout);
        std::fflush(stdout);
    };
    const auto reply = [&](const QJsonObject& request, const QJsonObject& body) {
        write({{"type", "response"},
               {"request_seq", request.value("seq")},
               {"command", request.value("command")},
               {"success", true},
               {"body", body}});
    };
    while (true) {
        const auto byte = std::getchar();
        if (byte == EOF)
            return 0;
        const auto messages = decoder.feed(QByteArray(1, static_cast<char>(byte)));
        if (!decoder.error().isEmpty())
            return 2;
        for (const auto& request : messages) {
            const auto command = request.value("command").toString();
            if (session) {
                if (command == "initialize") {
                    reply(request, {{"supportsConfigurationDoneRequest", true}});
                } else if (command == "launch" || command == "attach") {
                    pendingLaunch = request;
                    write({{"type", "event"}, {"event", "initialized"}});
                } else if (command == "setBreakpoints") {
                    reply(request, {{"breakpoints",
                                     request.value("arguments").toObject().value("breakpoints")}});
                } else if (command == "configurationDone") {
                    reply(request, {});
                    reply(pendingLaunch, {});
                    write(
                        {{"type", "event"},
                         {"event", "output"},
                         {"body", QJsonObject{{"category", "telemetry"}, {"output", "debugpy"}}}});
                    write({{"type", "event"},
                           {"event", "output"},
                           {"body", QJsonObject{{"category", "stdout"}, {"output", "hello"}}}});
                    write({{"type", "event"},
                           {"event", "output"},
                           {"body", QJsonObject{{"category", "stdout"}, {"output", " world\n"}}}});
                } else if (command == "disconnect") {
                    // An attach test uses this adapter's echo to audit detach policy.
                    const auto terminate =
                        request.value("arguments").toObject().value("terminateDebuggee").toBool();
                    if (pendingLaunch.value("command").toString() == "attach" && terminate)
                        return 7;
                    reply(request, {});
                    return 0;
                } else if (command == "pause" || command == "next" || command == "stepIn" ||
                           command == "stepOut") {
                    reply(request, {});
                    write({{"type", "event"},
                           {"event", "stopped"},
                           {"body", QJsonObject{{"threadId", 1}, {"reason", "step"}}}});
                } else if (command == "threads") {
                    reply(request,
                          {{"threads", QJsonArray{QJsonObject{{"id", 1}, {"name", "fixture"}}}}});
                } else if (command == "stackTrace") {
                    const QString sourcePath = pendingLaunch.value("arguments")
                                                   .toObject()
                                                   .value("program")
                                                   .toString("fixture.cpp");
                    const QJsonObject source{{"path", sourcePath}};
                    const QJsonObject frame{
                        {"id", 11}, {"name", "fixtureFrame"}, {"line", 7}, {"source", source}};
                    reply(request, {{"stackFrames", QJsonArray{frame}}});
                } else if (command == "scopes") {
                    const QJsonObject scope{{"name", "Locals"}, {"variablesReference", 12}};
                    reply(request, {{"scopes", QJsonArray{scope}}});
                } else if (command == "variables") {
                    const QJsonObject variable{
                        {"name", "answer"}, {"value", "42"}, {"variablesReference", 0}};
                    reply(request, {{"variables", QJsonArray{variable}}});
                } else if (command == "evaluate") {
                    reply(request, {{"result", "120"}, {"variablesReference", 0}});
                } else
                    reply(request, {});
                continue;
            }
            if (command == "exit")
                return 0;
            if (command == "crash")
                std::abort();
            if (command == "hold")
                continue;
            if (command == "malformed") {
                std::fputs("Content-Length: -1\r\n\r\n", stdout);
                std::fflush(stdout);
                continue;
            }
            const auto frame = litecode::debug::DapDecoder::encode(
                {{"seq", ++sequence},
                 {"type", "response"},
                 {"request_seq", request.value("seq")},
                 {"command", command},
                 {"success", true},
                 {"body", QJsonObject{{"echo", request.value("arguments")}}}});
            std::fwrite(frame.constData(), 1, static_cast<size_t>(frame.size()), stdout);
            std::fflush(stdout);
        }
    }
}
