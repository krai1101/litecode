#pragma once
#include "extensions/EventApi.h"
#include "extensions/WindowStateApi.h"
#include <QMutex>
#include <QMutexLocker>
#include <memory>

namespace litecode::extensions {
class HostWindowState final {
  public:
    explicit HostWindowState(WindowState initial = {true, true})
        : runtime_(std::make_shared<Runtime>()) {
        runtime_->state = initial;
    }
    ~HostWindowState() { shutdown(); }
    WindowState state() const {
        QMutexLocker lock(&runtime_->mutex);
        return runtime_->state;
    }
    Event<WindowState> onDidChangeWindowState() const { return runtime_->changed.event(); }
    // Transport updates run on the host dispatcher. Readers may use any thread.
    void accept(WindowState value) {
        const auto runtime = runtime_;
        for (const bool focus : {true, false}) {
            WindowState snapshot;
            {
                QMutexLocker lock(&runtime->mutex);
                if (runtime->stopped)
                    return;
                auto& current = focus ? runtime->state.focused : runtime->state.active;
                const bool next = focus ? value.focused : value.active;
                if (current == next)
                    continue;
                current = next;
                snapshot = runtime->state;
            }
            // Each property change produces a separate immutable snapshot.
            // Ownership survives listeners disposing the hosting facade.
            runtime->changed.fire(snapshot);
        }
    }
    void shutdown() {
        const auto runtime = runtime_;
        {
            QMutexLocker lock(&runtime->mutex);
            runtime->stopped = true;
        }
        runtime->changed.dispose();
    }

  private:
    struct Runtime {
        QMutex mutex;
        WindowState state;
        EventEmitter<WindowState> changed;
        bool stopped{};
    };
    std::shared_ptr<Runtime> runtime_;
};
} // namespace litecode::extensions
