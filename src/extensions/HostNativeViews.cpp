#include "extensions/HostNativeViews.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/NativeViewWire.h"
#include <QFutureWatcher>
#include <QPointer>
#include <QThread>
#include <stdexcept>
namespace litecode::extensions {
class HostNativeViews::View final : public NativeView {
  public:
    View(HostNativeViews* host, QString id, QString handle)
        : host_(host), id_(std::move(id)), handle_(std::move(handle)) {}
    QString viewType() const override { return id_; }
    bool visible() const override { return visible_; }
    void setContent(const NativeControl& root) override {
        send({{"action", "content"}, {"root", wire::encodeNativeControl(root)}});
    }
    void setText(const QString& id, const QString& text) override {
        send({{"action", "text"}, {"control", id}, {"text", text}});
    }
    void setEnabled(const QString& id, bool enabled) override {
        send({{"action", "enabled"}, {"control", id}, {"enabled", enabled}});
    }
    void scrollToEnd(const QString& id) override {
        send({{"action", "scrollToEnd"}, {"control", id}});
    }
    void show(bool preserveFocus) override {
        send({{"action", "show"}, {"preserveFocus", preserveFocus}});
    }
    Event<NativeViewEvent> onDidReceiveMessage() const override { return events_.event(); }
    Event<std::monostate> onDidChangeVisibility() const override { return visibility_.event(); }
    Event<std::monostate> onDidDispose() const override { return disposed_.event(); }
    void visibility(bool value) {
        if (visible_ == value || closed_)
            return;
        visible_ = value;
        visibility_.fire({});
    }
    void event(const NativeViewEvent& event, quint64 sequence) {
        inputSequence_ = sequence;
        if (!closed_)
            events_.fire(event);
    }
    void close() {
        if (closed_)
            return;
        closed_ = true;
        visible_ = false;
        disposed_.fire({});
        events_.dispose();
        visibility_.dispose();
        disposed_.dispose();
    }

  private:
    void send(QJsonObject message) {
        if (closed_ || !host_)
            throw std::runtime_error("Native view is disposed.");
        message.insert("inputSequence", QString::number(inputSequence_));
        message.insert("id", id_);
        message.insert("handle", handle_);
        host_->publish(std::move(message));
    }
    QPointer<HostNativeViews> host_;
    QString id_, handle_;
    bool visible_{true}, closed_{};
    quint64 inputSequence_{};
    EventEmitter<NativeViewEvent> events_;
    EventEmitter<std::monostate> visibility_, disposed_;
};
HostNativeViews::HostNativeViews(std::function<void(const QJsonObject&)> send)
    : send_(std::move(send)) {}
HostNativeViews::~HostNativeViews() { shutdown(); }
void HostNativeViews::publish(QJsonObject message) {
    message.insert("event", "nativeView");
    if (wire::encode(message).isEmpty())
        throw std::length_error("Native view exceeds transport limit.");
    const QPointer<HostNativeViews> self(this);
    QMetaObject::invokeMethod(
        this,
        [self, message = std::move(message)] {
            if (self && !self->stopped_)
                self->send_(message);
        },
        Qt::QueuedConnection);
}
Disposable HostNativeViews::registerProvider(const QString& id,
                                             std::shared_ptr<NativeViewProvider> provider) {
    if (QThread::currentThread() != thread() || stopped_ || id.isEmpty() || !provider ||
        registrations_.contains(id))
        throw std::invalid_argument("Native view provider unavailable or already registered.");
    const auto handle = QString::number(++nextHandle_);
    registrations_.insert(id, {std::move(provider), {}, {}, handle});
    publish({{"action", "register"}, {"id", id}, {"handle", handle}});
    const QPointer<HostNativeViews> self(this);
    return Disposable([self, id, handle] {
        if (self)
            QMetaObject::invokeMethod(
                self,
                [self, id, handle] {
                    if (self)
                        self->remove(id, handle);
                },
                Qt::QueuedConnection);
    });
}
void HostNativeViews::remove(const QString& id, const QString& handle) {
    auto it = registrations_.find(id);
    if (it == registrations_.end() || it->handle != handle)
        return;
    const auto registration = registrations_.take(id);
    // Publish removal before extension callbacks can register a replacement.
    if (!stopped_)
        publish({{"action", "unregister"}, {"id", id}, {"handle", handle}});
    if (registration.cancellation)
        registration.cancellation->cancel();
    if (registration.view)
        registration.view->close();
}
bool HostNativeViews::accept(const QJsonObject& message) {
    const auto id = message.value("id").toString(), handle = message.value("handle").toString();
    auto it = registrations_.find(id);
    if (it == registrations_.end() || it->handle != handle)
        return true; // Late traffic from a disposed registration.
    const auto action = message.value("action").toString();
    if (action == "resolve") {
        if (it->view)
            return true;
        it->view = std::make_shared<View>(this, id, handle);
        it->cancellation = std::make_shared<CancellationTokenSource>();
        const auto provider = it->provider;
        const auto view = it->view;
        const auto cancellation = it->cancellation;
        const auto failure = [this, id, handle](const QString& error) {
            const auto found = registrations_.constFind(id);
            if (found != registrations_.cend() && found->handle == handle)
                publish({{"action", "error"}, {"id", id}, {"handle", handle}, {"text", error}});
        };
        try {
            auto result = provider->resolveView(view, cancellation->token());
            if (auto* future = std::get_if<QFuture<void>>(&result)) {
                auto* watcher = new QFutureWatcher<void>(this);
                connect(watcher, &QFutureWatcher<void>::finished, this,
                        [watcher, provider, view, cancellation, failure] {
                            try {
                                watcher->future().waitForFinished();
                                if (watcher->future().isCanceled())
                                    failure("Native view resolution was canceled.");
                            } catch (const std::exception& error) {
                                failure(QString::fromUtf8(error.what()));
                            } catch (...) {
                                failure("Native view provider failed.");
                            }
                            watcher->deleteLater();
                        });
                watcher->setFuture(*future);
            }
        } catch (const std::exception& error) {
            failure(QString::fromUtf8(error.what()));
        } catch (...) {
            failure("Native view provider failed.");
        }
        return true;
    }
    if (!it->view)
        return false;
    const auto view = it->view;
    if (action == "visibility" && message.value("visible").isBool()) {
        view->visibility(message.value("visible").toBool());
        return true;
    }
    if (action == "input" && message.value("control").isString() &&
        message.value("type").isString() && message.value("text").isString() &&
        message.value("checked").isBool()) {
        view->event({message.value("control").toString(), message.value("type").toString(),
                     message.value("text").toString(), message.value("index").toInt(-1),
                     message.value("checked").toBool()},
                    message.value("inputSequence").toString().toULongLong());
        return true;
    }
    return false;
}
void HostNativeViews::shutdown() {
    if (stopped_)
        return;
    stopped_ = true;
    const auto registrations = std::exchange(registrations_, {});
    for (const auto& registration : registrations) {
        if (registration.cancellation)
            registration.cancellation->cancel();
        if (registration.view)
            registration.view->close();
    }
    send_ = {};
}
} // namespace litecode::extensions
