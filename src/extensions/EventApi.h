#pragma once
#include <QtGlobal>
#include <limits>
#include <concepts>
#include <QString>
#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <variant>
#include <vector>
#include <tuple>

namespace litecode::extensions {
namespace detail {
template<typename T> concept DisposableLike = requires(T& value) { value.dispose(); };
}
// A registration remains active until dispose() or extension shutdown, matching
// VS Code's Disposable semantics. Destroying the C++ handle does not unregister
// the contribution. Retain handles when early disposal is needed.
class Disposable final {
  public:
    Disposable() = default;
    explicit Disposable(std::function<void()> callback) {
        if (callback) state_ = std::make_shared<State>(std::move(callback));
    }
    // Copies are handles to the same registration, like JavaScript references.
    // Either handle can dispose it; cleanup still runs at most once.
    Disposable(const Disposable&) = default;
    Disposable& operator=(const Disposable&) = default;
    Disposable(Disposable&&) noexcept = default;
    Disposable& operator=(Disposable&&) noexcept = default;
    explicit operator bool() const noexcept {
        auto state = state_;
        if (!state) return false;
        std::lock_guard lock(state->mutex);
        return static_cast<bool>(state->callback);
    }
    void dispose() noexcept {
        auto state = state_;
        if (!state) return;
        std::function<void()> callback;
        {
            std::lock_guard lock(state->mutex);
            callback = std::exchange(state->callback, {});
        }
        if (callback) {
            try { callback(); } catch (...) { /* Native cleanup cannot unwind shutdown. */ }
        }
    }
    static Disposable from(std::vector<Disposable> values) {
        auto owned = std::make_shared<std::vector<Disposable>>(std::move(values));
        return Disposable([owned] {
            auto pending = std::exchange(*owned, {});
            for (auto& value : pending) value.dispose();
        });
    }
    template<detail::DisposableLike... Values>
    static Disposable from(Values... values) {
        auto owned = std::make_shared<std::tuple<Values...>>(std::move(values)...);
        return Disposable([owned] {
            std::apply([](auto&... value) {
                [[maybe_unused]] const auto cleanup = [](auto& item) {
                    try { (void)item.dispose(); } catch (...) {}
                };
                (cleanup(value), ...);
            }, *owned);
        });
    }
  private:
    struct State {
        explicit State(std::function<void()> value) : callback(std::move(value)) {}
        std::mutex mutex;
        std::function<void()> callback;
    };
    std::shared_ptr<State> state_;
};

namespace detail {
inline void disposeSubscriptions(std::vector<Disposable>& subscriptions) noexcept {
    // Never hold references into an extension-mutable vector across callbacks.
    // Drain new registrations too, without revisiting handles already detached.
    while (!subscriptions.empty()) {
        auto pending = std::exchange(subscriptions, {});
        for (auto& subscription : pending) subscription.dispose();
    }
}
template<typename T> struct EventSlot {
    std::function<void(const T&)> callback;
    std::atomic<bool> active{true};
};
template<typename T> struct EventDelivery {
    explicit EventDelivery(const T& data) : value(data) {}
    T value;
    std::vector<std::shared_ptr<EventSlot<T>>> listeners;
    size_t next{};
};
template<typename T> struct EventState {
    std::mutex mutex;
    std::recursive_mutex dispatchMutex;
    std::map<quint64, std::shared_ptr<EventSlot<T>>> listeners;
    std::shared_ptr<EventDelivery<T>> delivery;
    quint64 nextId{};
    bool disposed{};
};
} // namespace detail

template<typename T> class Event final {
  public:
    Event() = default;
    explicit Event(std::shared_ptr<detail::EventState<T>> state)
        : state_(std::move(state)) {}
    [[nodiscard]] Disposable operator()(std::function<void(const T&)> listener,
        std::vector<Disposable>* disposables = nullptr) const {
        auto state = state_.lock();
        if (!state || !listener) return {};
        auto slot = std::make_shared<detail::EventSlot<T>>();
        slot->callback = std::move(listener);
        quint64 id;
        {
            std::lock_guard lock(state->mutex);
            if (state->disposed || state->nextId == std::numeric_limits<quint64>::max()) return {};
            id = ++state->nextId;
            state->listeners.emplace(id, slot);
        }
        auto registration = Disposable([weakState = std::weak_ptr(state), weakSlot = std::weak_ptr(slot), id] {
            // Keep callback captures alive until after the registry lock has
            // been released. Their destructors can reenter the same event.
            auto currentSlot = weakSlot.lock();
            if (currentSlot) currentSlot->active = false;
            if (auto currentState = weakState.lock()) {
                std::lock_guard lock(currentState->mutex);
                currentState->listeners.erase(id);
            }
        });
        if (disposables) {
            try { disposables->push_back(registration); }
            catch (...) { registration.dispose(); throw; }
        }
        return registration;
    }
    template<typename Listener, typename Receiver>
        requires std::invocable<Listener, Receiver*, const T&>
    [[nodiscard]] Disposable operator()(Listener listener, Receiver* receiver,
        std::vector<Disposable>* disposables = nullptr) const {
        if (!receiver) return {};
        return (*this)([listener = std::move(listener), receiver](const T& value) {
            std::invoke(listener, receiver, value);
        }, disposables);
    }

  private:
    std::weak_ptr<detail::EventState<T>> state_;
};

template<typename T> class EventEmitter final {
  public:
    EventEmitter() : state_(std::make_shared<detail::EventState<T>>()) {}
    ~EventEmitter() { dispose(); }
    EventEmitter(const EventEmitter&) = delete;
    EventEmitter& operator=(const EventEmitter&) = delete;
    [[nodiscard]] Event<T> event() const { return Event<T>(state_); }
    void fire(const T& value) {
        auto state = state_;
        // Serialize native worker callers while permitting synchronous nested
        // fire on the delivery thread. Never hold the registry lock in callbacks.
        std::lock_guard dispatch(state->dispatchMutex);
        auto delivery = std::make_shared<detail::EventDelivery<T>>(value);
        // Upstream finishes pending listeners of the older event before a
        // nested fire starts delivering the new one.
        drain(state);
        {
            std::lock_guard lock(state->mutex);
            if (state->disposed) return;
            for (const auto& [id, slot] : state->listeners) delivery->listeners.push_back(slot);
            state->delivery = delivery;
        }
        drain(state);
    }
    void dispose() {
        auto state = state_;
        decltype(state->listeners) removed;
        std::shared_ptr<detail::EventDelivery<T>> pending;
        {
            std::lock_guard lock(state->mutex);
            if (state->disposed) return;
            state->disposed = true;
            for (const auto& [id, slot] : state->listeners) slot->active = false;
            removed.swap(state->listeners);
            pending = std::exchange(state->delivery, {});
        }
        // Release extension-owned captures outside the lock.
    }

  private:
    static void drain(const std::shared_ptr<detail::EventState<T>>& state) {
        for (;;) {
            std::shared_ptr<detail::EventDelivery<T>> current;
            std::shared_ptr<detail::EventSlot<T>> slot;
            {
                std::lock_guard lock(state->mutex);
                current = state->delivery;
                if (!current || state->disposed) return;
                if (current->next == current->listeners.size()) {
                    state->delivery.reset();
                    return;
                }
                // Advance before invoking: nested fire drains this same frame.
                slot = current->listeners[current->next++];
            }
            if (!slot->active) continue;
            try { slot->callback(current->value); } catch (...) {
                // A listener's failure must not suppress later listeners.
            }
        }
    }
    std::shared_ptr<detail::EventState<T>> state_;
};

} // namespace litecode::extensions
