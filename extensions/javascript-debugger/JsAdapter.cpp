#include "JsAdapter.h"
#include <QJsonArray>
#include <QRegularExpression>

namespace litecode::javascript {
namespace {
void translateThreadId(QJsonObject& value, const QString& key, int offset) {
    if (const auto id = value.value(key); id.isDouble())
        value.insert(key, id.toInt() + offset);
}
} // namespace
JsAdapter::JsAdapter(QString node, QString entryPoint)
    : node_(std::move(node)), entryPoint_(std::move(entryPoint)) {
    startup_.setSingleShot(true);
    connect(&startup_, &QTimer::timeout, this,
            [this] { fail("The JavaScript debug adapter did not open its local endpoint."); });
    connect(&server_, &QProcess::readyReadStandardOutput, this, [this] {
        const auto bytes = server_.readAllStandardOutput();
        if (port_ || disposed_)
            return;
        startupOutput_ += bytes;
        static const QRegularExpression address(
            R"(Debug server listening at 127\.0\.0\.1:(\d+)\r?\n)");
        const auto match = address.match(QString::fromUtf8(startupOutput_));
        if (match.hasMatch()) {
            port_ = match.captured(1).toInt();
            connectPeer(root_);
        } else if (startupOutput_.size() > 8192) {
            fail("The JavaScript debug adapter returned an invalid startup response.");
        }
    });
    connect(&server_, &QProcess::readyReadStandardError, this, [this] {
        const auto bytes = server_.readAllStandardError();
        if (!disposed_)
            output_.fire({{"type", "event"},
                          {"event", "output"},
                          {"seq", ++sequence_},
                          {"body", QJsonObject{{"category", "stderr"},
                                               {"output", QString::fromUtf8(bytes)}}}});
    });
    connect(&server_, &QProcess::errorOccurred, this, [this] { fail(server_.errorString()); });
    connect(&server_, &QProcess::finished, this,
            [this] { fail("The JavaScript debug adapter exited."); });
}

void JsAdapter::connectPeer(Connection& peer) {
    connect(&peer.socket, &debug::DapSocketConnection::connected, this, [this, &peer] {
        peer.connected = true;
        if (&peer == &root_) {
            startup_.stop();
            const auto pending = std::exchange(pending_, {});
            for (const auto& message : pending)
                send(root_, message);
        } else {
            send(child_, initialize_, false);
        }
    });
    connect(&peer.socket, &debug::DapSocketConnection::bytesReceived, this,
            [this, &peer](const QByteArray& bytes) {
                const auto messages = peer.decoder.feed(bytes);
                if (!peer.decoder.error().isEmpty()) {
                    fail(peer.decoder.error());
                    return;
                }
                for (const auto& message : messages) {
                    if (disposed_)
                        break;
                    receive(peer, message);
                }
            });
    connect(&peer.socket, &debug::DapSocketConnection::errorOccurred, this,
            [this](const QString& message) { fail(message); });
    connect(&peer.socket, &debug::DapSocketConnection::closed, this, [this, &peer] {
        if (&peer == &root_)
            fail("The JavaScript debug adapter disconnected.");
    });
    peer.socket.connectTcp("127.0.0.1", static_cast<quint16>(port_));
}

void JsAdapter::send(Connection& peer, QJsonObject message, bool forwardResponse) {
    if (disposed_)
        return;
    const int seq = ++peer.sequence;
    if (message.value("type") == "request")
        peer.requests.insert(seq, forwardResponse ? message.value("seq").toInt() : 0);
    if (&peer == &child_ && message.value("type") == "request") {
        auto args = message.value("arguments").toObject();
        if (args.contains("threadId")) {
            translateThreadId(args, "threadId", -1);
            message.insert("arguments", args);
        }
    }
    message.insert("seq", seq);
    if (!peer.socket.write(debug::DapDecoder::encode(message)))
        fail("Could not write to the JavaScript debug adapter.");
}

void JsAdapter::handleMessage(const QJsonObject& message) {
    if (disposed_)
        return;
    if (message.value("type") == "response") {
        const auto endpoint = reverseRequests_.take(message.value("request_seq").toInt());
        if (endpoint.first) {
            auto response = message;
            response.insert("request_seq", endpoint.second);
            send(*endpoint.first, response);
        }
        return;
    }
    const auto command = message.value("command").toString();
    if (command == "initialize") {
        initialize_ = message;
        auto arguments = message.value("arguments").toObject();
        arguments.insert("supportsStartDebuggingRequest", true);
        initialize_.insert("arguments", arguments);
        pending_.append(initialize_);
        if (!starting_) {
            starting_ = true;
            startup_.start(10000);
            server_.start(node_, {entryPoint_, "0", "127.0.0.1"});
        }
        return;
    }
    if (command == "launch" || command == "attach") {
        auto launch = message;
        auto arguments = message.value("arguments").toObject();
        arguments.insert("type", "pwa-node");
        launch.insert("arguments", arguments);
        send(root_, launch);
        return;
    }
    if (command == "disconnect") {
        ending_ = true;
        send(root_, message);
        return;
    }
    if (command == "setBreakpoints" || command == "setExceptionBreakpoints") {
        // Retain the root's initialization order so provisional breakpoint IDs
        // match the IDs allocated on the primary target connection.
        if (!childReady_)
            breakpointSettings_.append(message);
    }
    if (!root_.connected)
        pending_.append(message);
    else
        send(childReady_ ? child_ : root_, message);
}

void JsAdapter::receive(Connection& peer, QJsonObject message) {
    const auto type = message.value("type").toString();
    if (type == "request") {
        if (message.value("command") == "startDebugging") {
            const auto args = message.value("arguments").toObject();
            const auto config = args.value("configuration").toObject();
            if (&peer != &root_ || !childLaunch_.isEmpty() ||
                config.value("__pendingTargetId").toString().isEmpty()) {
                send(peer, {{"type", "response"},
                            {"request_seq", message.value("seq")},
                            {"command", "startDebugging"},
                            {"success", false},
                            {"message", "Only one Node.js target is supported."}});
                return;
            }
            childLaunch_ = {
                {"type", "request"}, {"command", args.value("request")}, {"arguments", config}};
            connectPeer(child_);
            send(peer, {{"type", "response"},
                        {"request_seq", message.value("seq")},
                        {"command", "startDebugging"},
                        {"success", true}});
            return;
        }
        reverseRequests_.insert(sequence_ + 1, {&peer, message.value("seq").toInt()});
    } else if (type == "response") {
        const int request = message.value("request_seq").toInt();
        const auto original = peer.requests.take(request);
        if (!original) {
            if (&peer == &child_) {
                if (!message.value("success").toBool())
                    fail(message.value("message").toString(
                        "The Node.js target could not initialize."));
                else if (request == childLaunchSequence_)
                    childReady_ = true;
            }
            return;
        }
        message.insert("request_seq", original);
    } else if (&peer == &child_ && message.value("event") == "initialized") {
        childLaunchSequence_ = child_.sequence + 1;
        send(child_, childLaunch_, false);
        for (const auto& setting : std::exchange(breakpointSettings_, {}))
            send(child_, setting, false);
        send(child_, {{"type", "request"}, {"command", "configurationDone"}}, false);
        return;
    }
    // js-debug can report a stop before replying to the child launch.
    // Route the stack and variables requested by that stop to the target.
    if (&peer == &child_ && message.value("event") == "stopped")
        childReady_ = true;
    if (message.value("event") == "terminated")
        ending_ = true;
    if (&peer == &child_) {
        // js-debug starts its thread IDs at zero. The workbench reserves zero
        // for the absence of a thread, so translate at the adapter boundary.
        auto body = message.value("body").toObject();
        if (type == "event" && body.contains("threadId")) {
            translateThreadId(body, "threadId", 1);
            message.insert("body", body);
        } else if (type == "response" && message.value("command") == "threads") {
            QJsonArray threads;
            for (const auto& entry : body.value("threads").toArray()) {
                auto thread = entry.toObject();
                translateThreadId(thread, "id", 1);
                threads.append(thread);
            }
            body.insert("threads", threads);
            message.insert("body", body);
        }
    }
    message.insert("seq", ++sequence_);
    output_.fire(message);
}

void JsAdapter::fail(const QString& message) {
    if (disposed_ || ending_)
        return;
    ending_ = true;
    if (!root_.connected) {
        reply(initialize_, false, message);
    } else {
        output_.fire({{"type", "event"},
                      {"event", "output"},
                      {"seq", ++sequence_},
                      {"body", QJsonObject{{"category", "stderr"}, {"output", message + '\n'}}}});
        output_.fire({{"type", "event"}, {"event", "terminated"}, {"seq", ++sequence_}});
    }
}

void JsAdapter::reply(const QJsonObject& request, bool success, const QString& message) {
    output_.fire({{"type", "response"},
                  {"seq", ++sequence_},
                  {"command", request.value("command")},
                  {"request_seq", request.value("seq")},
                  {"success", success},
                  {"message", message}});
}

void JsAdapter::dispose() {
    disposed_ = true;
    startup_.stop();
    child_.socket.close();
    root_.socket.close();
    if (server_.state() != QProcess::NotRunning)
        server_.kill();
    output_.dispose();
}
} // namespace litecode::javascript
