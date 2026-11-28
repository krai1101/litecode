#pragma once
#include "PythonInterpreters.h"
namespace litecode::python {
class PythonConfigurationProvider final : public QObject,
                                          public extensions::DebugConfigurationProvider {
  public:
    PythonConfigurationProvider(extensions::ExtensionContext& context,
                                std::shared_ptr<PythonInterpreters> interpreters, bool initial);
    extensions::ProviderResult<QVector<extensions::DebugConfiguration>>
    provideDebugConfigurations(const std::optional<extensions::WorkspaceFolder>& folder,
                               extensions::CancellationToken token) override;
    extensions::ProviderResult<extensions::DebugConfiguration>
    resolveDebugConfiguration(const std::optional<extensions::WorkspaceFolder>& folder,
                              extensions::DebugConfiguration configuration,
                              extensions::CancellationToken token) override;
    extensions::ProviderResult<extensions::DebugConfiguration>
    resolveDebugConfigurationWithSubstitutedVariables(
        const std::optional<extensions::WorkspaceFolder>& folder,
        extensions::DebugConfiguration configuration, extensions::CancellationToken token) override;

  private:
    extensions::ExtensionContext& context_;
    std::shared_ptr<PythonInterpreters> interpreters_;
    bool initial_;
};
class PythonAdapterFactory final : public QObject,
                                   public extensions::DebugAdapterDescriptorFactory {
  public:
    PythonAdapterFactory(extensions::ExtensionContext& context,
                         std::shared_ptr<PythonInterpreters> interpreters,
                         std::shared_ptr<extensions::LogOutputChannel> output);
    extensions::ProviderResult<extensions::DebugAdapterDescriptor>
    createDebugAdapterDescriptor(std::shared_ptr<extensions::DebugSession> session,
                                 std::optional<extensions::DebugAdapterExecutable>) override;

  private:
    extensions::ExtensionContext& context_;
    std::shared_ptr<PythonInterpreters> interpreters_;
    std::shared_ptr<extensions::LogOutputChannel> output_;
};
} // namespace litecode::python
