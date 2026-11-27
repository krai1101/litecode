#include "extensions/HostProgress.h"
#include "extensions/ProgressWire.h"
#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QTimer>
#include <limits>
#include <cmath>

namespace litecode::extensions {
struct ProgressState {
    quint64 id{};
    std::shared_ptr<CancellationTokenSource> cancellation;
    ProgressReport pending;
    bool hasPending{}, scheduled{}, finished{};
    qint64 lastSent{-100};
};
struct ProgressRuntime {
    QMutex mutex;
    QObject* dispatcher{};
    bool stopped{};
    quint64 nextId{};
    QElapsedTimer clock;
    QHash<quint64, std::shared_ptr<ProgressState>> active;
    std::function<void(const QJsonObject&)> send;
};
namespace {
QJsonObject reportMessage(const ProgressState& state, const ProgressReport& report) {
    return {{"event", "progress"}, {"operation", "report"}, {"id", QString::number(state.id)}, {"report", wire::encodeProgressReport(report)}};
}
void flush(const std::shared_ptr<ProgressRuntime>& runtime, const std::shared_ptr<ProgressState>& state) {
    ProgressReport report;
    {
        QMutexLocker lock(&runtime->mutex);
        if (runtime->stopped || state->finished || !state->hasPending) return;
        const auto remaining = 100 - (runtime->clock.elapsed() - state->lastSent);
        if (remaining > 0) {
            QTimer::singleShot(static_cast<int>(remaining), runtime->dispatcher, [runtime, state] { flush(runtime, state); });
            return;
        }
        report = state->pending;
        state->pending = {};
        state->hasPending = state->scheduled = false;
        state->lastSent = runtime->clock.elapsed();
    }
    runtime->send(reportMessage(*state, report));
}
class Reporter final : public Progress<ProgressReport> {
  public:
    Reporter(std::shared_ptr<ProgressRuntime> runtime, std::shared_ptr<ProgressState> state) : runtime_(std::move(runtime)), state_(std::move(state)) {}
    void report(const ProgressReport& value) override {
        if (!wire::decodeProgressReport(wire::encodeProgressReport(value))) throw std::invalid_argument("Invalid progress report.");
        QMutexLocker lock(&runtime_->mutex);
        if (runtime_->stopped || state_->finished) return;
        if (value.increment) {
            const auto increment = state_->pending.increment.value_or(0) + *value.increment;
            if (!std::isfinite(increment)) throw std::invalid_argument("Progress increment overflow.");
            state_->pending.increment = increment;
        }
        state_->pending.message = value.message;
        state_->hasPending = true;
        if (state_->scheduled) return;
        state_->scheduled = true;
        QMetaObject::invokeMethod(runtime_->dispatcher, [runtime = runtime_, state = state_] { flush(runtime, state); }, Qt::QueuedConnection);
    }
    void finish() {
        std::shared_ptr<CancellationTokenSource> cancellation;
        {
            QMutexLocker lock(&runtime_->mutex);
            if (state_->finished) return;
            state_->finished = true;
            runtime_->active.remove(state_->id);
            cancellation = state_->cancellation;
            if (!runtime_->stopped) {
                const auto pending = state_->pending;
                const auto hasPending = state_->hasPending;
                QMetaObject::invokeMethod(runtime_->dispatcher, [runtime = runtime_, state = state_, pending, hasPending] {
                    { QMutexLocker lock(&runtime->mutex); if (runtime->stopped) return; }
                    if (hasPending) runtime->send(reportMessage(*state, pending));
                    runtime->send({{"event", "progress"}, {"operation", "end"}, {"id", QString::number(state->id)}});
                }, Qt::QueuedConnection);
            }
        }
        if (cancellation) cancellation->dispose();
    }
  private:
    std::shared_ptr<ProgressRuntime> runtime_;
    std::shared_ptr<ProgressState> state_;
};
} // namespace
HostProgress::HostProgress(std::function<void(const QJsonObject&)> send) : runtime_(std::make_shared<ProgressRuntime>()) {
    runtime_->dispatcher = this;
    runtime_->send = std::move(send);
    runtime_->clock.start();
}
HostProgress::~HostProgress() { shutdown(); }
detail::ProgressSession HostProgress::begin(const ProgressOptions& options) {
    const auto encoded = wire::encodeProgressOptions(options);
    if (!wire::decodeProgressOptions(encoded)) throw std::invalid_argument("Invalid progress options.");
    QMutexLocker lock(&runtime_->mutex);
    if (runtime_->stopped || runtime_->nextId == std::numeric_limits<quint64>::max()) throw std::runtime_error("Progress service unavailable.");
    auto state = std::make_shared<ProgressState>();
    state->id = ++runtime_->nextId;
    if (options.cancellable.value_or(false)) state->cancellation = std::make_shared<CancellationTokenSource>();
    runtime_->active.insert(state->id, state);
    QMetaObject::invokeMethod(this, [runtime = runtime_, state, encoded] {
        { QMutexLocker lock(&runtime->mutex); if (runtime->stopped) return; }
        runtime->send({{"event", "progress"}, {"operation", "start"}, {"id", QString::number(state->id)}, {"options", encoded}});
    }, Qt::QueuedConnection);
    auto reporter = std::make_shared<Reporter>(runtime_, state);
    return {reporter, state->cancellation ? state->cancellation->token() : CancellationToken{}, [reporter] { reporter->finish(); }};
}
void HostProgress::cancel(quint64 id) {
    std::shared_ptr<CancellationTokenSource> cancellation;
    { QMutexLocker lock(&runtime_->mutex); const auto state = runtime_->active.value(id); if (state && !state->finished) cancellation = state->cancellation; }
    if (cancellation) cancellation->cancel();
}
void HostProgress::shutdown() {
    QHash<quint64, std::shared_ptr<ProgressState>> active;
    {
        QMutexLocker lock(&runtime_->mutex);
        if (runtime_->stopped) return;
        runtime_->stopped = true;
        runtime_->dispatcher = nullptr;
        active = std::exchange(runtime_->active, {});
        for (const auto& state : active) state->finished = true;
    }
    for (const auto& state : active) if (state->cancellation) { state->cancellation->cancel(); state->cancellation->dispose(); }
}
} // namespace litecode::extensions
