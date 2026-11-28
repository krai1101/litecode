#include "extensions/HostInlineDebugAdapter.h"
#include <QMutex>
#include <QMutexLocker>
#include <QUuid>
#include <atomic>
#include <stdexcept>

namespace litecode::extensions {
namespace {
constexpr qsizetype MaxQueuedBytes = 32 * 1024 * 1024;
}
struct HostInlineDebugAdapter::Delivery {
    QMutex mutex;
    HostInlineDebugAdapter* receiver{};
    std::atomic<qsizetype> queued{};
    bool failed{};
};
HostInlineDebugAdapter::HostInlineDebugAdapter(std::shared_ptr<DebugAdapter> adapter,
                                               QObject* parent)
    : QObject(parent), adapter_(std::move(adapter)), delivery_(std::make_shared<Delivery>()) {
    if (!adapter_)
        throw std::invalid_argument("Inline debug adapter implementation is null.");
    // Construction failures must dispose a returned native adapter too.
    try {
        server_.setSocketOptions(QLocalServer::UserAccessOption);
        if (!server_.listen("lc-dap-" + QUuid::createUuid().toString(QUuid::Id128)))
            throw std::runtime_error(server_.errorString().toStdString());
        delivery_->receiver = this;
        subscription_ =
            adapter_->onDidSendMessage()([delivery = delivery_](const QJsonObject& message) {
                const auto frame = debug::DapDecoder::encode(message);
                QMutexLocker lock(&delivery->mutex);
                auto* receiver = delivery->receiver;
                if (!receiver || delivery->failed)
                    return;
                if (frame.isEmpty() || delivery->queued.load() + frame.size() > MaxQueuedBytes) {
                    delivery->failed = true;
                    QMetaObject::invokeMethod(
                        receiver, [receiver] { receiver->stop(); }, Qt::QueuedConnection);
                    return;
                }
                delivery->queued.fetch_add(frame.size());
                QMetaObject::invokeMethod(
                    receiver,
                    [receiver, delivery, frame] {
                        delivery->queued.fetch_sub(frame.size());
                        receiver->sendFrame(frame);
                    },
                    Qt::QueuedConnection);
            });
        if (!subscription_)
            throw std::invalid_argument("Inline debug adapter message event is unavailable.");
        connect(&server_, &QLocalServer::newConnection, this, [this] { accept(); });
    } catch (...) {
        stop();
        throw;
    }
}
HostInlineDebugAdapter::~HostInlineDebugAdapter() { stop(); }
void HostInlineDebugAdapter::stop() noexcept {
    if (stopped_)
        return;
    stopped_ = true;
    {
        QMutexLocker lock(&delivery_->mutex);
        delivery_->receiver = nullptr;
    }
    subscription_.dispose();
    server_.close();
    if (socket_) {
        socket_->disconnect(this);
        socket_->abort();
    }
    output_.clear();
    auto adapter = std::exchange(adapter_, {});
    if (adapter) {
        try {
            adapter->dispose();
        } catch (...) {
        }
    }
    // QObject ownership cancels queued deliveries on deletion. Shutdown can
    // delete us synchronously first; deleteLater is harmless in that case.
    deleteLater();
}
void HostInlineDebugAdapter::accept() {
    if (stopped_ || socket_)
        return;
    socket_ = server_.nextPendingConnection();
    if (!socket_)
        return;
    socket_->setParent(this);
    socket_->setReadBufferSize(debug::DapDecoder::MaxPayloadBytes +
                               debug::DapDecoder::MaxHeaderBytes);
    server_.close(); // exactly one peer per descriptor
    connect(socket_, &QLocalSocket::readyRead, this, [this] { receive(); });
    connect(socket_, &QLocalSocket::bytesWritten, this, [this] { flush(); });
    connect(socket_, &QLocalSocket::disconnected, this, [this] { stop(); });
    connect(socket_, &QLocalSocket::errorOccurred, this, [this](auto) { stop(); });
    flush();
    receive();
}
void HostInlineDebugAdapter::receive() {
    if (stopped_ || !socket_)
        return;
    const auto messages = decoder_.feed(socket_->readAll());
    if (!decoder_.error().isEmpty()) {
        stop();
        return;
    }
    for (const auto& message : messages) {
        if (stopped_)
            return;
        auto adapter = adapter_;
        try {
            adapter->handleMessage(message);
        } catch (...) {
            stop();
            return;
        }
    }
}
void HostInlineDebugAdapter::sendFrame(const QByteArray& frame) {
    if (stopped_)
        return;
    const auto buffered = output_.size() + (socket_ ? socket_->bytesToWrite() : 0);
    if (frame.size() > MaxQueuedBytes - buffered) {
        stop();
        return;
    }
    output_.append(frame);
    flush();
}
void HostInlineDebugAdapter::flush() {
    if (stopped_ || !socket_ || output_.isEmpty())
        return;
    const auto written = socket_->write(output_);
    if (written < 0) {
        stop();
        return;
    }
    output_.remove(0, written);
}
} // namespace litecode::extensions
