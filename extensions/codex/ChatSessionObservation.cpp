#include "ChatSession.h"
#include "TurnHistory.h"
#include <QFutureWatcher>
#include <QtConcurrentRun>
namespace litecode::codex {
void ChatSession::readObservedPage(const QJsonObject& page, const QString& path, quint64 epoch,
                                   std::function<void(QJsonObject)> done) {
    const auto data = page.value("data").toArray();
    const auto latest = data.isEmpty() ? QJsonObject{} : data.first().toObject();
    if (path.isEmpty() || latest.value("status") != "interrupted" ||
        latest.value("completedAt").isDouble() || latest.value("durationMs").isDouble()) {
        done(page);
        return;
    }
    // Do not scan a large external transcript on the extension's event loop.
    // This is read-only and never resumes or takes over the other app's writer.
    auto* watcher = new QFutureWatcher<ObservedTurnLifecycle>(this);
    connect(watcher, &QFutureWatcher<ObservedTurnLifecycle>::finished, this,
            [this, watcher, epoch, page, done = std::move(done)]() mutable {
                const auto lifecycle = watcher->result();
                watcher->deleteLater();
                if (epoch != epoch_)
                    return;
                observedLifecycle_ = lifecycle;
                auto observed = page;
                auto turns = observed.value("data").toArray();
                turns[0] = reconcileObservedTurn(turns[0].toObject(), lifecycle);
                observed.insert("data", turns);
                done(std::move(observed));
            });
    const auto id = latest.value("id").toString();
    const auto cached = observedLifecycle_;
    observationCancelled_->store(true);
    observationCancelled_ = std::make_shared<std::atomic_bool>(false);
    const auto cancelled = observationCancelled_;
    watcher->setFuture(QtConcurrent::run(&observationPool_, [path, id, cached, cancelled] {
        return readObservedTurnLifecycle(path, id, cached, cancelled.get());
    }));
}
void ChatSession::setViewVisible(bool visible) {
    viewVisible_ = visible;
    updateObservation();
}
void ChatSession::updateObservation() {
    if (!readOnly_ || opening_ || threadId_.isEmpty() || !server_.ready() ||
        (accountKnown_ && requiresAuth_ && account_.isEmpty())) {
        observationTimer_.stop();
        observationInFlight_ = false;
        observedTail_ = {};
        observationError_.clear();
    } else if (viewVisible_) {
        if (!observationTimer_.isActive())
            observationTimer_.start();
    } else {
        observationTimer_.stop();
    }
}
bool ChatSession::hasRunningTurn() const {
    for (const auto& turn : turns_)
        if (turn.status == "inProgress" || turn.status == "in_progress")
            return true;
    return false;
}
void ChatSession::refreshObservedThread() {
    if (!readOnly_ || !viewVisible_ || opening_ || !server_.ready() || threadId_.isEmpty() ||
        observationInFlight_)
        return;
    observationInFlight_ = true;
    const auto epoch = epoch_;
    server_.request(
        "thread/turns/list",
        {{"threadId", threadId_}, {"limit", 1}, {"sortDirection", "desc"}, {"itemsView", "full"}},
        [this, epoch](const QJsonObject& page, const QString& error) {
            if (epoch != epoch_ || !readOnly_)
                return;
            if (!error.isEmpty()) {
                observationInFlight_ = false;
                if (observationError_ != error)
                    emit this->error(error);
                observationError_ = error;
                return;
            }
            readObservedPage(
                page, observedRolloutPath_, epoch, [this, epoch](const QJsonObject& page) {
                    observationError_.clear();
                    const auto data = page.value("data").toArray();
                    if (data.isEmpty() || data.first().toObject() == observedTail_) {
                        observationInFlight_ = false;
                        return;
                    }
                    const auto latest = data.first().toObject();
                    if (!turns_.isEmpty() && latest.value("id").toString() == turns_.last().id) {
                        observedTail_ = latest;
                        readTurnHistory(latest, turns_.last());
                        observationInFlight_ = false;
                        emit changed(turns_.last().id);
                        emit stateChanged();
                        return;
                    }
                    // A new external turn appeared. Fetch the recent page once so we
                    // also receive the preceding turn's completion, not just its successor.
                    server_.request(
                        "thread/turns/list",
                        {{"threadId", threadId_},
                         {"limit", 8},
                         {"sortDirection", "desc"},
                         {"itemsView", "full"}},
                        [this, epoch](const QJsonObject& recent, const QString& failure) {
                            if (epoch != epoch_ || !readOnly_)
                                return;
                            if (!failure.isEmpty()) {
                                observationInFlight_ = false;
                                if (observationError_ != failure)
                                    emit this->error(failure);
                                observationError_ = failure;
                                return;
                            }
                            readObservedPage(
                                recent, observedRolloutPath_, epoch,
                                [this](const QJsonObject& recent) {
                                    observationInFlight_ = false;
                                    const auto values = recent.value("data").toArray();
                                    if (values.isEmpty())
                                        return;
                                    int prefix = -1;
                                    for (const auto& value : values)
                                        for (int i = 0; i < turns_.size(); ++i)
                                            if (turns_[i].id ==
                                                    value.toObject().value("id").toString() &&
                                                (prefix < 0 || i < prefix))
                                                prefix = i;
                                    if (prefix < 0) {
                                        turns_.clear();
                                        turnsCursor_ = recent.value("nextCursor").toString();
                                    } else {
                                        turns_.resize(prefix);
                                    }
                                    for (auto it = values.end(); it != values.begin();) {
                                        TurnState turn;
                                        readTurnHistory((*--it).toObject(), turn);
                                        turns_.append(std::move(turn));
                                    }
                                    observedTail_ = values.first().toObject();
                                    emit changed({});
                                    emit stateChanged();
                                });
                        });
                });
        });
}
} // namespace litecode::codex
