#pragma once
#include "extensions/EventApi.h"
#include <QTimer>
#include <stdexcept>
#include <variant>

namespace litecode::extensions {
namespace detail {
struct CancellationState {
    std::mutex mutex;
    bool cancelled{};
    bool materialized{};
    bool fixedNone{};
    std::shared_ptr<EventEmitter<std::monostate>> emitter;
};
} // namespace detail

class CancellationToken final {
  public:
    CancellationToken() = default;
    bool isCancellationRequested() const {
        if (!state_)
            return false;
        std::lock_guard lock(state_->mutex);
        return state_->cancelled;
    }
    [[nodiscard]] Disposable
    onCancellationRequested(std::function<void()> listener,
                            std::vector<Disposable>* disposables = nullptr) const {
        if (!state_ || !listener)
            return {};
        {
            std::lock_guard lock(state_->mutex);
            if (!state_->cancelled) {
                if (state_->fixedNone)
                    return {};
                if (!state_->emitter)
                    state_->emitter = std::make_shared<EventEmitter<std::monostate>>();
                // Cancel cannot fire between obtaining the emitter and subscribing.
                return state_->emitter->event()(
                    [listener = std::move(listener)](const std::monostate&) { listener(); },
                    disposables);
            }
        }
        auto active = std::make_shared<std::atomic<bool>>(true);
        auto registration = Disposable([active] { *active = false; });
        if (disposables)
            disposables->push_back(registration);
        QTimer::singleShot(0, [active, listener = std::move(listener)] {
            if (active->exchange(false)) {
                try {
                    listener();
                } catch (...) {
                }
            }
        });
        return registration;
    }
    template <typename Listener, typename Receiver>
        requires std::invocable<Listener, Receiver*>
    [[nodiscard]] Disposable
    onCancellationRequested(Listener listener, Receiver* receiver,
                            std::vector<Disposable>* disposables = nullptr) const {
        if (!receiver)
            return {};
        return onCancellationRequested(
            [listener = std::move(listener), receiver] { std::invoke(listener, receiver); },
            disposables);
    }

  private:
    friend class CancellationTokenSource;
    explicit CancellationToken(std::shared_ptr<detail::CancellationState> state)
        : state_(std::move(state)) {}
    std::shared_ptr<detail::CancellationState> state_;
};

class CancellationTokenSource final {
  public:
    CancellationTokenSource() : state_(std::make_shared<detail::CancellationState>()) {}
    ~CancellationTokenSource() { dispose(); }
    CancellationTokenSource(const CancellationTokenSource&) = delete;
    CancellationTokenSource& operator=(const CancellationTokenSource&) = delete;
    [[nodiscard]] CancellationToken token() const {
        std::lock_guard lock(state_->mutex);
        state_->materialized = true;
        return CancellationToken(state_);
    }
    void cancel() {
        auto state = state_;
        std::shared_ptr<EventEmitter<std::monostate>> emitter;
        {
            std::lock_guard lock(state->mutex);
            if (state->cancelled || state->fixedNone)
                return;
            state->cancelled = true;
            emitter = state->emitter;
        }
        // A listener may release the source itself. Everything used after
        // callbacks is retained locally, independent of the source's lifetime.
        if (emitter) {
            emitter->fire({});
            {
                std::lock_guard lock(state->mutex);
                if (state->emitter == emitter)
                    state->emitter.reset();
            }
            emitter->dispose();
        }
    }
    void dispose() {
        auto state = state_;
        std::shared_ptr<EventEmitter<std::monostate>> emitter;
        {
            std::lock_guard lock(state->mutex);
            if (!state->materialized && !state->cancelled)
                state->fixedNone = true;
            emitter = std::exchange(state->emitter, {});
        }
        // A materialized uncancelled token stays mutable: new subscriptions
        // recreate its emitter, following upstream MutableToken.dispose().
        if (emitter)
            emitter->dispose();
    }

  private:
    std::shared_ptr<detail::CancellationState> state_;
};

class CancellationError final : public std::runtime_error {
  public:
    CancellationError() : std::runtime_error("Canceled") {}
};
} // namespace litecode::extensions
