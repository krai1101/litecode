#pragma once
#include "extensions/DebugApi.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QPromise>
#include <QTimer>
#include <stdexcept>

class FixtureDebugTracker final : public litecode::extensions::DebugAdapterTracker {
  public:
    explicit FixtureDebugTracker(std::shared_ptr<QJsonObject> state) : state_(std::move(state)) {}
    void onWillStartSession() override { increment("starts"); }
    void onWillStopSession() override { increment("stops"); }
    void onError(const std::exception& error) override {
        increment("errors");
        state_->insert("error", QString::fromUtf8(error.what()));
    }
    void onExit(std::optional<int> code, std::optional<QString>) override {
        increment("exits");
        if (code)
            state_->insert("exitCode", *code);
        else
            state_->remove("exitCode");
    }
    void onWillReceiveMessage(const QJsonObject& message) override {
        increment("receives");
        auto commands = state_->value("commands").toArray();
        commands.append(message.value("command"));
        state_->insert("commands", commands);
    }
    void onDidSendMessage(const QJsonObject& message) override {
        increment("sends");
        if (message.value("command") == QJsonValue("fixtureEcho"))
            state_->insert("customResponse", message.value("body"));
    }

  private:
    void increment(const QString& key) { state_->insert(key, state_->value(key).toInt() + 1); }
    std::shared_ptr<QJsonObject> state_;
};
class FixtureDebugTrackerFactory final : public litecode::extensions::DebugAdapterTrackerFactory {
  public:
    explicit FixtureDebugTrackerFactory(std::shared_ptr<QJsonObject> state)
        : state_(std::move(state)) {}
    litecode::extensions::ProviderResult<std::shared_ptr<litecode::extensions::DebugAdapterTracker>>
    createDebugAdapterTracker(
        std::shared_ptr<litecode::extensions::DebugSession> session) override {
        using namespace litecode::extensions;
        state_->insert("factoryCalls", state_->value("factoryCalls").toInt() + 1);
        state_->insert("sessionId", session->id());
        auto tracker =
            std::shared_ptr<DebugAdapterTracker>(std::make_shared<FixtureDebugTracker>(state_));
        const auto mode = session->configuration().value("trackerFixtureMode").toString();
        if (mode == "throw")
            throw std::runtime_error("fixture tracker factory failure");
        if (mode == "absent")
            return std::optional<std::shared_ptr<DebugAdapterTracker>>{};
        if (mode == "async" || mode == "slow") {
            auto promise = std::make_shared<QPromise<std::shared_ptr<DebugAdapterTracker>>>();
            promise->start();
            const auto result = promise->future();
            QTimer::singleShot(mode == "slow" ? 1500 : 20, QCoreApplication::instance(),
                               [promise, tracker] {
                                   promise->addResult(tracker);
                                   promise->finish();
                               });
            return result;
        }
        return tracker;
    }

  private:
    std::shared_ptr<QJsonObject> state_;
};
