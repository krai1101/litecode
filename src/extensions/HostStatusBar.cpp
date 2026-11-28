#include "extensions/HostStatusBar.h"
#include "extensions/ExtensionWireProtocol.h"
#include "extensions/StatusBarWire.h"
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QPointer>
#include <QTimer>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace litecode::extensions {
struct StatusBarEntryState {
    quint64 token{}, generation{};
    wire::StatusBarState values;
    bool visible{}, disposed{}, scheduled{};
};
struct StatusBarRuntime {
    QMutex mutex;
    QObject* dispatcher{};
    QString owner;
    std::function<void(const QJsonObject&)> send;
    QHash<quint64, std::shared_ptr<StatusBarEntryState>> entries;
    QHash<quint64, std::shared_ptr<Disposable>> messages;
    QHash<quint64, QPointer<QTimer>> timers;
    quint64 nextToken{};
    bool stopped{};
};
namespace {
void sendChecked(const std::function<void(const QJsonObject&)>& send, const QJsonObject& message) {
    try {
        if (wire::encode(message).isEmpty()) {
            send({{"event", "statusBar"},
                  {"op", "error"},
                  {"text", "Status bar update exceeds the transport limit."}});
        } else
            send(message);
    } catch (...) { /* A void UI update cannot unwind the Qt event loop. */
    }
}
// All enqueue operations hold the runtime mutex. Shutdown clears dispatcher
// under the same mutex before the QObject can be destroyed.
void post(const std::shared_ptr<StatusBarRuntime>& runtime, QJsonObject message) {
    if (runtime->stopped || !runtime->dispatcher)
        return;
    QMetaObject::invokeMethod(
        runtime->dispatcher,
        [runtime, message = std::move(message)] {
            std::function<void(const QJsonObject&)> send;
            {
                QMutexLocker lock(&runtime->mutex);
                if (runtime->stopped)
                    return;
                send = runtime->send;
            }
            sendChecked(send, message);
        },
        Qt::QueuedConnection);
}
QJsonObject remove(const std::shared_ptr<StatusBarEntryState>& state) {
    QJsonObject message{
        {"event", "statusBar"}, {"op", "remove"}, {"token", QString::number(state->token)}};
    if (state->values.itemId)
        message.insert("itemId", *state->values.itemId);
    return message;
}
void update(const std::shared_ptr<StatusBarRuntime>& runtime,
            const std::shared_ptr<StatusBarEntryState>& state) {
    if (runtime->stopped || state->disposed || !state->visible || state->scheduled)
        return;
    state->scheduled = true;
    const auto generation = state->generation;
    QMetaObject::invokeMethod(
        runtime->dispatcher,
        [runtime, state, generation] {
            std::function<void(const QJsonObject&)> send;
            QJsonObject message;
            {
                QMutexLocker lock(&runtime->mutex);
                if (runtime->stopped || state->disposed || !state->visible ||
                    state->generation != generation)
                    return;
                state->scheduled = false;
                auto values = state->values;
                if (values.backgroundColor)
                    values.color =
                        ThemeColor(values.backgroundColor->id() == "statusBarItem.errorBackground"
                                       ? "statusBarItem.errorForeground"
                                       : "statusBarItem.warningForeground");
                message = {{"event", "statusBar"},
                           {"op", "set"},
                           {"token", QString::number(state->token)},
                           {"state", wire::encodeStatusBarState(values)}};
                send = runtime->send;
            }
            sendChecked(send, message);
        },
        Qt::QueuedConnection);
}
class Entry final : public StatusBarItem {
  public:
    Entry(std::shared_ptr<StatusBarRuntime> runtime, std::shared_ptr<StatusBarEntryState> state)
        : runtime_(std::move(runtime)), state_(std::move(state)) {}
    QString id() const override {
        QMutexLocker lock(&runtime_->mutex);
        return state_->values.itemId.value_or(runtime_->owner);
    }
    StatusBarAlignment alignment() const override {
        QMutexLocker lock(&runtime_->mutex);
        return state_->values.alignment;
    }
    std::optional<double> priority() const override {
        QMutexLocker lock(&runtime_->mutex);
        return state_->values.priority;
    }
    QString text() const override { return read(&wire::StatusBarState::text); }
    void text(QString value) override { write(&wire::StatusBarState::text, std::move(value)); }
    std::optional<QString> name() const override { return read(&wire::StatusBarState::name); }
    void name(std::optional<QString> value) override {
        write(&wire::StatusBarState::name, std::move(value));
    }
    std::optional<StatusBarTooltip> tooltip() const override {
        return read(&wire::StatusBarState::tooltip);
    }
    void tooltip(std::optional<StatusBarTooltip> value) override {
        write(&wire::StatusBarState::tooltip, std::move(value));
    }
    std::optional<StatusBarColor> color() const override {
        return read(&wire::StatusBarState::color);
    }
    void color(std::optional<StatusBarColor> value) override {
        write(&wire::StatusBarState::color, std::move(value));
    }
    std::optional<ThemeColor> backgroundColor() const override {
        return read(&wire::StatusBarState::backgroundColor);
    }
    void backgroundColor(std::optional<ThemeColor> value) override {
        if (value && value->id() != "statusBarItem.errorBackground" &&
            value->id() != "statusBarItem.warningBackground")
            value.reset();
        write(&wire::StatusBarState::backgroundColor, std::move(value));
    }
    std::optional<StatusBarCommand> command() const override {
        return read(&wire::StatusBarState::command);
    }
    void command(std::optional<StatusBarCommand> value) override {
        write(&wire::StatusBarState::command, std::move(value));
    }
    std::optional<AccessibilityInformation> accessibilityInformation() const override {
        return read(&wire::StatusBarState::accessibilityInformation);
    }
    void accessibilityInformation(std::optional<AccessibilityInformation> value) override {
        write(&wire::StatusBarState::accessibilityInformation, std::move(value));
    }
    void show() override {
        QMutexLocker lock(&runtime_->mutex);
        if (runtime_->stopped || state_->disposed)
            return;
        state_->visible = true;
        update(runtime_, state_);
    }
    void hide() override {
        QMutexLocker lock(&runtime_->mutex);
        if (runtime_->stopped || state_->disposed)
            return;
        hideLocked();
    }
    void dispose() override {
        QMutexLocker lock(&runtime_->mutex);
        if (state_->disposed)
            return;
        hideLocked();
        state_->disposed = true;
        runtime_->entries.remove(state_->token);
    }

  private:
    template <typename T> T read(T wire::StatusBarState::* member) const {
        QMutexLocker lock(&runtime_->mutex);
        return state_->values.*member;
    }
    template <typename T> void write(T wire::StatusBarState::* member, T value) {
        QMutexLocker lock(&runtime_->mutex);
        state_->values.*member = std::move(value);
        update(runtime_, state_);
    }
    void hideLocked() {
        state_->visible = false;
        state_->scheduled = false;
        ++state_->generation;
        post(runtime_, remove(state_));
    }
    std::shared_ptr<StatusBarRuntime> runtime_;
    std::shared_ptr<StatusBarEntryState> state_;
};
} // namespace
HostStatusBar::HostStatusBar(QString owner, std::function<void(const QJsonObject&)> send)
    : runtime_(std::make_shared<StatusBarRuntime>()) {
    runtime_->dispatcher = this;
    runtime_->owner = std::move(owner);
    runtime_->send = std::move(send);
}
HostStatusBar::~HostStatusBar() { shutdown(); }
std::shared_ptr<StatusBarItem> HostStatusBar::create(std::optional<QString> id,
                                                     StatusBarAlignment alignment,
                                                     std::optional<double> priority) {
    QMutexLocker lock(&runtime_->mutex);
    if (runtime_->stopped)
        throw std::runtime_error("Status bar service has stopped.");
    if (runtime_->nextToken == std::numeric_limits<quint64>::max())
        throw std::overflow_error("Status bar identifiers exhausted.");
    if (priority) {
        if (std::isnan(*priority))
            priority.reset();
        else if (std::isinf(*priority))
            priority = std::copysign(std::numeric_limits<double>::max(), *priority);
    }
    auto state = std::make_shared<StatusBarEntryState>();
    state->token = ++runtime_->nextToken;
    state->values.itemId = std::move(id);
    state->values.priority = priority;
    state->values.alignment = alignment == StatusBarAlignment::Left ? StatusBarAlignment::Left
                                                                    : StatusBarAlignment::Right;
    runtime_->entries.insert(state->token, state);
    return std::make_shared<Entry>(runtime_, state);
}
Disposable HostStatusBar::message(const QString& text, std::optional<double> timeout) {
    auto runtime = runtime_;
    quint64 token = 0;
    Disposable disposable;
    {
        QMutexLocker lock(&runtime->mutex);
        if (runtime->stopped)
            return {};
        if (runtime->nextToken == std::numeric_limits<quint64>::max())
            throw std::overflow_error("Status bar identifiers exhausted.");
        token = ++runtime->nextToken;
        const std::weak_ptr<StatusBarRuntime> weak(runtime);
        disposable = Disposable([weak, token] {
            if (const auto alive = weak.lock()) {
                QMutexLocker lock(&alive->mutex);
                if (alive->messages.remove(token)) {
                    post(alive, {{"event", "statusBar"},
                                 {"op", "messageRemove"},
                                 {"token", QString::number(token)}});
                    const auto timer = alive->timers.take(token);
                    if (timer && alive->dispatcher)
                        QMetaObject::invokeMethod(
                            alive->dispatcher,
                            [timer] {
                                if (timer) {
                                    timer->stop();
                                    timer->deleteLater();
                                }
                            },
                            Qt::QueuedConnection);
                }
            }
        });
        runtime->messages.insert(token, std::make_shared<Disposable>(disposable));
        post(runtime, {{"event", "statusBar"},
                       {"op", "message"},
                       {"token", QString::number(token)},
                       {"text", text}});
        if (timeout) {
            const int milliseconds = !std::isfinite(*timeout) || *timeout < 1 ||
                                             *timeout > std::numeric_limits<int>::max()
                                         ? 1
                                         : static_cast<int>(*timeout);
            QMetaObject::invokeMethod(
                runtime->dispatcher,
                [runtime, token, milliseconds] {
                    QTimer* timer = nullptr;
                    {
                        QMutexLocker lock(&runtime->mutex);
                        if (runtime->stopped || !runtime->messages.contains(token))
                            return;
                        timer = new QTimer(runtime->dispatcher);
                        runtime->timers.insert(token, timer);
                    }
                    timer->setSingleShot(true);
                    QObject::connect(timer, &QTimer::timeout, timer, [runtime, token, timer] {
                        std::shared_ptr<Disposable> message;
                        {
                            QMutexLocker lock(&runtime->mutex);
                            message = runtime->messages.value(token);
                        }
                        if (message)
                            message->dispose();
                        timer->deleteLater();
                    });
                    timer->start(milliseconds);
                },
                Qt::QueuedConnection);
        }
    }
    return disposable;
}
void HostStatusBar::shutdown() {
    QHash<quint64, std::shared_ptr<Disposable>> messages;
    {
        QMutexLocker lock(&runtime_->mutex);
        if (runtime_->stopped)
            return;
        runtime_->stopped = true;
        runtime_->dispatcher = nullptr;
        runtime_->send = {};
        for (const auto& timer : runtime_->timers)
            if (timer)
                timer->deleteLater();
        runtime_->timers.clear();
        for (const auto& state : runtime_->entries) {
            state->disposed = true;
            state->visible = false;
        }
        runtime_->entries.clear();
        messages.swap(runtime_->messages);
    }
    for (const auto& message : messages)
        message->dispose();
}
} // namespace litecode::extensions
