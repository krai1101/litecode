#pragma once
#include "extensions/DebugConfigurationApi.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonObject>
#include <QPromise>
#include <QTimer>
#include <stdexcept>

class FixtureDynamicDebugConfigurationProvider final
    : public litecode::extensions::DebugConfigurationProvider {
  public:
    litecode::extensions::ProviderResult<QVector<litecode::extensions::DebugConfiguration>>
    provideDebugConfigurations(const std::optional<litecode::extensions::WorkspaceFolder>&,
                               litecode::extensions::CancellationToken) override {
        return QVector<litecode::extensions::DebugConfiguration>{
            QJsonObject{{"name", "Dynamic native launch"},
                        {"type", "fixture"},
                        {"request", "launch"},
                        {"inlineFixture", true},
                        {"program", "${file}"}}};
    }
};

class FixtureDebugConfigurationProvider final
    : public litecode::extensions::DebugConfigurationProvider {
  public:
    explicit FixtureDebugConfigurationProvider(QString stage, bool provides = false)
        : stage_(std::move(stage)), provides_(provides) {}
    litecode::extensions::ProviderResult<QVector<litecode::extensions::DebugConfiguration>>
    provideDebugConfigurations(const std::optional<litecode::extensions::WorkspaceFolder>& folder,
                               litecode::extensions::CancellationToken) override {
        if (!provides_)
            return std::optional<QVector<litecode::extensions::DebugConfiguration>>{};
        return QVector<litecode::extensions::DebugConfiguration>{
            QJsonObject{{"name", "Generated native launch"},
                        {"type", "fixture"},
                        {"request", "launch"},
                        {"inlineFixture", true},
                        {"providerStages", true},
                        {"program", "${workspaceFolder}/sample.py"},
                        {"folderSeen", folder ? folder->uri.toString() : QString()}}};
    }
    litecode::extensions::ProviderResult<litecode::extensions::DebugConfiguration>
    resolveDebugConfiguration(const std::optional<litecode::extensions::WorkspaceFolder>&,
                              litecode::extensions::DebugConfiguration configuration,
                              litecode::extensions::CancellationToken token) override {
        auto value = configuration.toObject();
        if (!value.value("providerStages").toBool())
            return configuration;
        if (value.value("cancelAs").toString() == "null")
            return QJsonValue(QJsonValue::Null);
        if (value.value("cancelAs").toString() == "undefined")
            return QJsonValue(QJsonValue::Undefined);
        auto stages = value.value("stages").toArray();
        stages.append("before:" + stage_);
        value.insert("stages", stages);
        if (!value.value("asyncProvider").toBool())
            return QJsonValue(value);
        auto promise = std::make_shared<QPromise<litecode::extensions::DebugConfiguration>>();
        promise->start();
        const auto future = promise->future();
        QTimer::singleShot(100, QCoreApplication::instance(), [promise, token, value] {
            promise->addResult(token.isCancellationRequested() ? QJsonValue(QJsonValue::Undefined)
                                                               : QJsonValue(value));
            promise->finish();
        });
        return future;
    }
    litecode::extensions::ProviderResult<litecode::extensions::DebugConfiguration>
    resolveDebugConfigurationWithSubstitutedVariables(
        const std::optional<litecode::extensions::WorkspaceFolder>&,
        litecode::extensions::DebugConfiguration configuration,
        litecode::extensions::CancellationToken) override {
        auto value = configuration.toObject();
        if (value.value("providerStages").toBool()) {
            auto stages = value.value("stages").toArray();
            stages.append("after:" + stage_);
            value.insert("stages", stages);
            if (value.value("program").toString().contains("${"))
                throw std::runtime_error("Variables were not substituted before the second phase.");
        }
        return QJsonValue(value);
    }

  private:
    QString stage_;
    bool provides_{};
};
