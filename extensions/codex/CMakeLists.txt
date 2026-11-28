include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/Runtime.cmake)
add_library(litecode_codex_approval STATIC CodexApproval.cpp CodexApproval.h)
target_link_libraries(litecode_codex_approval PUBLIC LiteCode::ExtensionSDK PRIVATE litecode_project_options)
target_include_directories(litecode_codex_approval PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(litecode_codex_approval PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(litecode_codex STATIC ConversationMarkdown.cpp ConversationMarkdown.h ToolCallDisplay.cpp ToolCallDisplay.h ObservedToolActivity.cpp ObservedToolActivity.h ChatSessionObservation.cpp ObservedTurnLifecycle.cpp ObservedTurnLifecycle.h TurnHistory.cpp TurnHistory.h CodexExecutionPolicy.cpp CodexExecutionPolicy.h AppServer.cpp AppServer.h ChatSession.cpp ChatSessionAuth.cpp ChatSessionOpening.cpp ChatSession.h TurnUndo.cpp TurnUndo.h)
target_include_directories(litecode_codex PUBLIC ${PROJECT_SOURCE_DIR}/extensions)
set_target_properties(litecode_codex PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_link_libraries(litecode_codex PUBLIC Qt6::Core PRIVATE Qt6::Concurrent litecode_project_options)
target_compile_definitions(litecode_codex PRIVATE LITECODE_VERSION="${PROJECT_VERSION}")

set(LITECODE_CODEX_EXTENSION_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(LITECODE_CODEX_EXTENSION_ARCH MATCHES "^(AMD64|amd64|x86_64)$")
    set(LITECODE_CODEX_EXTENSION_ARCH x86_64)
elseif(LITECODE_CODEX_EXTENSION_ARCH MATCHES "^(ARM64|arm64|aarch64)$")
    set(LITECODE_CODEX_EXTENSION_ARCH arm64)
endif()
configure_file(extension.json.in extension.json @ONLY)
set_source_files_properties(CodexExtension.cpp PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/extension.json")
add_library(litecode_codex_extension MODULE CodexExtension.cpp CodexViewProvider.cpp CodexViewProvider.h
    CodexViewLayout.cpp CodexAuthLayout.cpp CodexAuthActions.cpp CodexViewLayout.h CodexViewControls.h CodexComposerLayout.cpp
    CodexHistoryWindow.cpp CodexHistoryWindow.h CodexConversationLayout.cpp CodexHistoryLayout.cpp CodexTurnActions.cpp CodexTurnActions.h)
target_sources(litecode_codex_extension PRIVATE CodexViewSnapshot.cpp CodexViewActions.cpp CodexComposerActions.cpp CodexModelSelection.cpp CodexDiffDocuments.cpp CodexDiffDocuments.h)
target_sources(litecode_codex_extension PRIVATE CodexApprovalActions.cpp CodexApprovalLayout.cpp)
target_sources(litecode_codex_extension PRIVATE CodexThreadLink.cpp CodexThreadLink.h CodexLinkActions.cpp CodexMenuLayout.cpp CodexMenuActions.cpp CodexTurnPresentation.h CodexTurnPresentation.cpp
    CodexWorkLayout.cpp CodexChangesLayout.cpp CodexViewIcons.h CodexViewIcons.cpp
    CodexAttachmentStore.h CodexAttachmentStore.cpp)
target_include_directories(litecode_codex_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_codex_extension PRIVATE LiteCode::ExtensionSDK litecode_codex litecode_codex_approval litecode_project_options)
set(_codex_extension_directory "${CMAKE_BINARY_DIR}/extension-packages/codex")
set_target_properties(litecode_codex_extension PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_codex_extension_directory}"
    RUNTIME_OUTPUT_DIRECTORY "${_codex_extension_directory}")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/codex.svg "${_codex_extension_directory}/codex.svg" COPYONLY)
file(COPY "${CMAKE_CURRENT_SOURCE_DIR}/assets/lucide/" DESTINATION "${_codex_extension_directory}/icons")
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/codex-wordmark.svg "${_codex_extension_directory}/icons/codex-wordmark.svg" COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/codex-home.svg "${_codex_extension_directory}/icons/codex-home.svg" COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/LICENSE "${_codex_extension_directory}/LICENSE" COPYONLY)
configure_file(README.md "${_codex_extension_directory}/README.md" COPYONLY)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/assets/ATTRIBUTION.md "${_codex_extension_directory}/licenses/OPENAI-ICON.md" COPYONLY)
foreach(_notice CODEX-APACHE-2.0.txt CODEX-NOTICE.txt CODEX-ZSH.txt)
    configure_file("${PROJECT_SOURCE_DIR}/licenses/${_notice}" "${_codex_extension_directory}/licenses/${_notice}" COPYONLY)
endforeach()
add_custom_command(TARGET litecode_codex_extension POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory "${LITECODE_CODEX_DIRECTORY}" "${_codex_extension_directory}/runtime"
    COMMENT "Staging the extension-owned Codex runtime" VERBATIM)
install(DIRECTORY "${_codex_extension_directory}/" DESTINATION extensions/codex USE_SOURCE_PERMISSIONS)
