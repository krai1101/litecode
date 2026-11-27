include(CMakePackageConfigHelpers)

# Public SDK contains only the native API and its QObject meta-object. Plugins
# must not link the manager or any private workbench implementation.
add_library(litecode_extension_sdk STATIC ExtensionApi.h ConfigurationApi.h WorkspaceApi.h FileSystemApi.h TextApi.h DocumentApi.h DocumentOpenApi.h ContentProviderApi.h
    MarkdownApi.h MarkdownApi.cpp ThemeApi.h OutputApi.h MessageApi.h WindowApi.h DialogApi.h InputBoxApi.h QuickPickApi.h
    CommandApi.h StatusBarApi.h
    ProviderApi.h UriApi.h
    WindowStateApi.h
    ChatApi.h
    NativeViewApi.h
    EditorEditApi.h
    EditorApi.h
    DebugApi.h DebugConfigurationApi.h DebugTrackerApi.h DebugStartApi.h WorkspaceFolderApi.h
    PrimitivesApi.h EventApi.h CancellationApi.h ProgressApi.h WorkspaceEditApi.h SnippetApi.h SnippetApi.cpp EnvironmentApi.h LogApi.h)
set_target_properties(litecode_extension_sdk PROPERTIES
    EXPORT_NAME ExtensionSDK
    POSITION_INDEPENDENT_CODE ON
)
add_library(LiteCode::ExtensionSDK ALIAS litecode_extension_sdk)
target_compile_features(litecode_extension_sdk PUBLIC cxx_std_20)
target_include_directories(litecode_extension_sdk PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/src>
    $<INSTALL_INTERFACE:sdk/include>
)
target_link_libraries(litecode_extension_sdk PUBLIC Qt6::Core)

add_library(litecode_extension_wire STATIC
    NativeViewWire.h NativeViewWire.cpp
    ExtensionWireProtocol.h ExtensionWireProtocol.cpp
    ConfigurationModel.h ConfigurationModel.cpp
    ConfigurationWire.h ConfigurationWire.cpp
    ChatRequestWire.h ChatRequestWire.cpp
    ChatHistoryWire.h ChatHistoryWire.cpp
    MarkdownWire.h MarkdownWire.cpp
    IconWire.h IconWire.cpp
    DialogWire.h DialogWire.cpp
    InputBoxWire.h InputBoxWire.cpp
    QuickPickWire.h QuickPickWire.cpp
    StatusBarWire.h StatusBarWire.cpp
    UriWire.h UriWire.cpp
    WindowStateWire.h
    ProgressWire.h ProgressWire.cpp
    DocumentWire.h DocumentWire.cpp DocumentChangeWire.cpp
    TextContentWire.h TextContentWire.cpp
    WorkspaceEditWire.h WorkspaceEditWire.cpp
    TextEditorEditData.h TextEditorEditData.cpp
    TextEditorEditWire.h TextEditorEditWire.cpp
    EditorStateWire.h EditorStateWire.cpp
    DebugDescriptorWire.h DebugDescriptorWire.cpp
    DebugAdapterContribution.h DebugAdapterContribution.cpp
    WorkspaceEditTransfer.h WorkspaceEditTransfer.cpp
    JsonMessageTransfer.h JsonMessageTransfer.cpp
    EnvironmentInfo.h EnvironmentInfo.cpp
    DocumentTextBuffer.h DocumentTextBuffer.cpp
    ChatStreamLedger.h ChatStreamLedger.cpp)
target_include_directories(litecode_extension_wire PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(litecode_extension_wire PUBLIC Qt6::Core LiteCode::ExtensionSDK)

add_library(litecode_extensions STATIC
    ExtensionManager.h ExtensionManager.cpp ExtensionManifest.cpp
    NativeExtensionInstaller.h NativeExtensionInstaller.cpp
    NativeExtensionUninstaller.h NativeExtensionUninstaller.cpp
    WorkbenchNativeViews.h WorkbenchNativeViews.cpp NativeViewInputLedger.h NativeViewInputLedger.cpp
    WorkbenchDebugConfigurations.h WorkbenchDebugConfigurations.cpp
    WorkbenchDebugSessions.h WorkbenchDebugSessions.cpp
    WorkbenchDebugTrackers.h WorkbenchDebugTrackers.cpp
    WorkbenchDebugStarts.h WorkbenchDebugStarts.cpp
    ConfigurationFile.h ConfigurationFile.cpp
    ConfigurationRegistry.h ConfigurationRegistry.cpp
    ConfigurationService.h ConfigurationService.cpp
    ConfigurationRequests.h ConfigurationRequests.cpp
    WorkbenchDocuments.h WorkbenchDocuments.cpp
    WorkbenchContentProviders.h WorkbenchContentProviders.cpp
    WorkbenchOutput.h WorkbenchOutput.cpp
    WorkbenchMessages.h WorkbenchMessages.cpp
    WorkbenchDialogs.h WorkbenchDialogs.cpp
    WorkbenchInputBox.h WorkbenchInputBox.cpp
    WorkbenchQuickPick.h WorkbenchQuickPick.cpp
    WorkbenchQuickInputCoordinator.h WorkbenchQuickInputCoordinator.cpp
    WorkbenchStatusBar.h WorkbenchStatusBar.cpp
    WorkbenchExternalUris.h WorkbenchExternalUris.cpp
    WorkbenchUriHandlers.h WorkbenchUriHandlers.cpp
    WorkbenchProgress.h WorkbenchProgress.cpp
    WorkbenchClipboard.h WorkbenchClipboard.cpp
    WorkbenchEnvironment.h WorkbenchEnvironment.cpp
    ChatHistory.h ChatHistory.cpp)
target_include_directories(litecode_extensions PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(litecode_extensions PUBLIC LiteCode::ExtensionSDK PRIVATE
    Qt6::Network Qt6::Concurrent litecode_extension_wire litecode_jsonc litecode_project_options)

add_executable(litecode_extension_host ExtensionHostMain.cpp
    HostNativeViews.h HostNativeViews.cpp
    HostDebug.h HostDebug.cpp
    HostDebugConfigurations.h HostDebugConfigurations.cpp
    HostDebugSessions.h HostDebugSessions.cpp
    HostDebugTrackers.h HostDebugTrackers.cpp
    HostDebugStarts.h HostDebugStarts.cpp
    HostInlineDebugAdapter.h HostInlineDebugAdapter.cpp
    HostEditors.h HostEditors.cpp
    HostConfiguration.h HostConfiguration.cpp
    HostWorkspace.h
    HostDocumentOpening.h
    HostDocumentOpening.cpp
    HostContentProviders.h HostContentProviders.cpp
    LocalFileSystem.h LocalFileSystem.cpp
    HostTextDocument.h HostTextDocument.cpp
    HostTextEditorEdit.h HostTextEditorEdit.cpp
    HostDocuments.h HostDocuments.cpp
    HostDocumentRequests.h HostDocumentRequests.cpp
    HostWorkspaceRequests.h HostWorkspaceRequests.cpp
    HostOutput.h HostOutput.cpp
    HostMessages.h HostMessages.cpp
    HostWindow.h
    HostChatParticipant.h HostChatParticipant.cpp
    HostWindowState.h
    HostDialogs.h HostDialogs.cpp
    HostInputBox.h HostInputBox.cpp
    HostQuickPick.h HostQuickPick.cpp
    HostStatusBar.h HostStatusBar.cpp
    HostUriHandlers.h HostUriHandlers.cpp
    HostExternalUris.h HostExternalUris.cpp
    HostProgress.h HostProgress.cpp
    HostClipboard.h HostClipboard.cpp
    HostEnvironment.h
    ExtensionMemento.h ExtensionMemento.cpp
    SecretEncryptionBackend.h
    SecretOperationQueue.h
    ExtensionSecretStorage.h ExtensionSecretStorage.cpp)
target_link_libraries(litecode_extension_host PRIVATE LiteCode::ExtensionSDK
    Qt6::Network Qt6::Concurrent litecode_extension_wire litecode_debug litecode_project_options)
if(WIN32)
    target_sources(litecode_extension_host PRIVATE SecretEncryptionWindows.cpp)
    target_link_libraries(litecode_extension_host PRIVATE Crypt32)
elseif(APPLE OR CMAKE_SYSTEM_NAME STREQUAL "Linux")
    include(${PROJECT_SOURCE_DIR}/cmake/SecretStorage.cmake)
    target_sources(litecode_extension_host PRIVATE SecretEncryptionKeychain.cpp)
    litecode_link_secret_storage(litecode_extension_host)
else()
    message(FATAL_ERROR "Native secret storage currently supports Windows, macOS and Linux.")
endif()
set_target_properties(litecode_extension_host PROPERTIES
    OUTPUT_NAME LiteCodeExtensionHost
    WIN32_EXECUTABLE TRUE
)
if(APPLE)
    install(TARGETS litecode_extension_host RUNTIME DESTINATION LiteCode.app/Contents/MacOS)
else()
    install(TARGETS litecode_extension_host RUNTIME DESTINATION bin)
endif()

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/LiteCodeExtensionSDKConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/LiteCodeExtensionSDKConfig.cmake
    INSTALL_DESTINATION sdk/lib/cmake/LiteCodeExtensionSDK
)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/LiteCodeExtensionSDKConfigVersion.cmake
    VERSION 16.0.0 COMPATIBILITY ExactVersion
)
install(TARGETS litecode_extension_sdk EXPORT LiteCodeExtensionSDKTargets
    ARCHIVE DESTINATION sdk/lib
)
install(EXPORT LiteCodeExtensionSDKTargets NAMESPACE LiteCode::
    DESTINATION sdk/lib/cmake/LiteCodeExtensionSDK
)
install(FILES ExtensionApi.h ConfigurationApi.h WorkspaceApi.h FileSystemApi.h TextApi.h DocumentApi.h DocumentOpenApi.h ContentProviderApi.h WorkspaceEditApi.h SnippetApi.h EnvironmentApi.h LogApi.h MarkdownApi.h ThemeApi.h OutputApi.h MessageApi.h WindowApi.h DialogApi.h InputBoxApi.h QuickPickApi.h PrimitivesApi.h EventApi.h CancellationApi.h ProgressApi.h DESTINATION sdk/include/extensions)
install(FILES WindowStateApi.h DESTINATION sdk/include/extensions)
install(FILES ChatApi.h DESTINATION sdk/include/extensions)
install(FILES NativeViewApi.h DESTINATION sdk/include/extensions)
install(FILES EditorEditApi.h DESTINATION sdk/include/extensions)
install(FILES EditorApi.h DESTINATION sdk/include/extensions)
install(FILES DebugApi.h DebugConfigurationApi.h DebugTrackerApi.h DebugStartApi.h WorkspaceFolderApi.h DESTINATION sdk/include/extensions)
install(FILES CommandApi.h StatusBarApi.h DESTINATION sdk/include/extensions)
install(FILES ProviderApi.h UriApi.h DESTINATION sdk/include/extensions)
install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/LiteCodeExtensionSDKConfig.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/LiteCodeExtensionSDKConfigVersion.cmake
    DESTINATION sdk/lib/cmake/LiteCodeExtensionSDK
)
