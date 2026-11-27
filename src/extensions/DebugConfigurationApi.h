#pragma once
#include "extensions/CancellationApi.h"
#include "extensions/ProviderApi.h"
#include "extensions/WorkspaceFolderApi.h"
#include <QJsonValue>
#include <QVector>

namespace litecode::extensions {
// An object is a configuration; undefined cancels; null cancels and requests
// opening launch.json. QJsonValue preserves that stable three-way distinction.
using DebugConfiguration = QJsonValue;
enum class DebugConfigurationProviderTriggerKind { Initial = 1, Dynamic = 2 };
class DebugConfigurationProvider {
  public:
    virtual ~DebugConfigurationProvider() = default;
    virtual ProviderResult<QVector<DebugConfiguration>>
    provideDebugConfigurations(const std::optional<WorkspaceFolder>&, CancellationToken = {}) {
        return std::optional<QVector<DebugConfiguration>>{};
    }
    virtual ProviderResult<DebugConfiguration>
    resolveDebugConfiguration(const std::optional<WorkspaceFolder>&,
                              DebugConfiguration configuration, CancellationToken = {}) {
        return configuration;
    }
    virtual ProviderResult<DebugConfiguration>
    resolveDebugConfigurationWithSubstitutedVariables(const std::optional<WorkspaceFolder>&,
                                                      DebugConfiguration configuration,
                                                      CancellationToken = {}) {
        return configuration;
    }
};
} // namespace litecode::extensions
