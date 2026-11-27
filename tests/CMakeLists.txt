function(add_litecode_qtest test_name target)
    if(WIN32)
        # GUI Qt tests have no console output on Windows; save their result for CI diagnostics.
        add_test(NAME ${test_name} COMMAND ${target} -o ${test_name}.qtest.txt,txt)
    else()
        add_test(NAME ${test_name} COMMAND ${target})
    endif()
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
    if(WIN32)
        get_target_property(test_libraries ${target} LINK_LIBRARIES)
        if("${test_libraries}" MATCHES "litecode_ui|Qt6::Widgets")
            # Native GUI suites share desktop focus and must not compete for it.
            set_tests_properties(${test_name} PROPERTIES RESOURCE_LOCK litecode_qt_desktop)
        endif()
    endif()
endfunction()
add_executable(litecode_dap_protocol_tests tst_dap_protocol.cpp)
target_link_libraries(litecode_dap_protocol_tests PRIVATE litecode_debug Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.debug.dap_protocol litecode_dap_protocol_tests)
add_executable(litecode_dap_adapter_fixture dap_adapter_fixture.cpp)
target_link_libraries(litecode_dap_adapter_fixture PRIVATE litecode_debug litecode_project_options)
add_executable(litecode_dap_transport_tests tst_dap_transport.cpp)
target_link_libraries(litecode_dap_transport_tests PRIVATE litecode_debug Qt6::Network Qt6::Test litecode_project_options)
target_compile_definitions(litecode_dap_transport_tests PRIVATE
    LITECODE_DAP_ADAPTER_FIXTURE="$<TARGET_FILE:litecode_dap_adapter_fixture>")
add_dependencies(litecode_dap_transport_tests litecode_dap_adapter_fixture)
add_litecode_qtest(litecode.debug.dap_transport litecode_dap_transport_tests)
add_executable(litecode_debug_session_tests tst_debug_session.cpp)
target_link_libraries(litecode_debug_session_tests PRIVATE litecode_debug Qt6::Network Qt6::Test litecode_project_options)
target_compile_definitions(litecode_debug_session_tests PRIVATE
    LITECODE_DAP_ADAPTER_FIXTURE="$<TARGET_FILE:litecode_dap_adapter_fixture>")
add_dependencies(litecode_debug_session_tests litecode_dap_adapter_fixture)
add_litecode_qtest(litecode.debug.session litecode_debug_session_tests)
add_executable(litecode_debug_configuration_tests tst_debug_configuration.cpp)
target_link_libraries(litecode_debug_configuration_tests PRIVATE litecode_debug Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.debug.configuration litecode_debug_configuration_tests)
add_executable(litecode_task_configuration_tests tst_task_configuration.cpp)
target_link_libraries(litecode_task_configuration_tests PRIVATE litecode_debug Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.debug.task_configuration litecode_task_configuration_tests)
add_executable(litecode_diff_editor_tests tst_diff_editor.cpp)
target_link_libraries(litecode_diff_editor_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.ui.diff_editor litecode_diff_editor_tests)

add_executable(litecode_codex_runtime_tests tst_codex_runtime.cpp)
add_executable(litecode_workbench_tabs_tests tst_workbench_tabs.cpp)
target_link_libraries(litecode_workbench_tabs_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.ui.workbench_tabs litecode_workbench_tabs_tests)
add_executable(litecode_workbench_titlebar_tests tst_workbench_titlebar.cpp)
target_link_libraries(litecode_workbench_titlebar_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.ui.workbench_titlebar litecode_workbench_titlebar_tests)
add_executable(litecode_git_tests tst_git_service.cpp)
add_executable(litecode_source_control_ui_tests tst_source_control_ui.cpp)
add_dependencies(litecode_source_control_ui_tests litecode_git_askpass)
add_dependencies(litecode_source_control_ui_tests litecode_dap_adapter_fixture)
target_compile_definitions(litecode_source_control_ui_tests PRIVATE
    LITECODE_GIT_ASKPASS_HELPER="$<TARGET_FILE:litecode_git_askpass>"
    LITECODE_DAP_ADAPTER_FIXTURE="$<TARGET_FILE:litecode_dap_adapter_fixture>")
add_custom_command(TARGET litecode_source_control_ui_tests POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:litecode_git_askpass>"
        "$<TARGET_FILE_DIR:litecode_source_control_ui_tests>"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "$<TARGET_FILE:Qt6::Network>"
        "$<TARGET_FILE_DIR:litecode_source_control_ui_tests>"
    VERBATIM)
target_link_libraries(litecode_source_control_ui_tests PRIVATE litecode_ui scintilla_qt Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.source_control.ui litecode_source_control_ui_tests)
add_executable(litecode_source_control_manual_tests EXCLUDE_FROM_ALL manual_source_control_smoke.cpp)
target_link_libraries(litecode_source_control_manual_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
target_link_libraries(litecode_git_tests PRIVATE litecode_workspace Qt6::Network Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.git litecode_git_tests)
# The integration suite launches dozens of real Git processes and a cancelable
# HTTP fixture. On loaded Windows runners it can exceed the generic 120 s limit
# even when every assertion passes; keep a bounded but realistic allowance.
set_tests_properties(litecode.git PROPERTIES TIMEOUT 180)
add_executable(litecode_extension_tests tst_extensions.cpp)
add_executable(litecode_codex_runtime_fixture codex_runtime_fixture.cpp)
target_link_libraries(litecode_codex_runtime_fixture PRIVATE Qt6::Core litecode_project_options)
add_executable(litecode_extension_native_view_tests tst_extension_native_views.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexChangesLayout.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostNativeViews.cpp)
target_link_libraries(litecode_extension_native_view_tests PRIVATE litecode_ui litecode_extension_wire Qt6::Test litecode_project_options)
target_include_directories(litecode_extension_native_view_tests PRIVATE ${PROJECT_SOURCE_DIR}/extensions/codex)
target_compile_definitions(litecode_extension_native_view_tests PRIVATE
    LITECODE_CODEX_PLUGIN="$<TARGET_FILE:litecode_codex_extension>"
    LITECODE_CODEX_FIXTURE="$<TARGET_FILE:litecode_codex_runtime_fixture>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_extension_native_view_tests litecode_codex_extension litecode_codex_runtime_fixture litecode_extension_host)
add_litecode_qtest(litecode.extensions.native_views litecode_extension_native_view_tests)
add_executable(litecode_native_view_reconcile_tests tst_native_view_reconcile.cpp)
target_link_libraries(litecode_native_view_reconcile_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.native_view_reconcile litecode_native_view_reconcile_tests)
add_executable(litecode_codex_thread_opening_tests tst_codex_thread_opening.cpp)
target_link_libraries(litecode_codex_thread_opening_tests PRIVATE litecode_codex Qt6::Test litecode_project_options)
target_compile_definitions(litecode_codex_thread_opening_tests PRIVATE
    LITECODE_CODEX_FIXTURE="$<TARGET_FILE:litecode_codex_runtime_fixture>")
add_dependencies(litecode_codex_thread_opening_tests litecode_codex_runtime_fixture)
add_litecode_qtest(litecode.codex.thread_opening litecode_codex_thread_opening_tests)
add_executable(litecode_codex_presentation_tests tst_codex_presentation.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexApprovalLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexThreadLink.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexMenuLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexViewIcons.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexAttachmentStore.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexTurnPresentation.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexWorkLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexViewLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexAuthLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexHistoryWindow.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexHistoryLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexComposerLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexModelSelection.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexConversationLayout.cpp
    ${PROJECT_SOURCE_DIR}/extensions/codex/CodexChangesLayout.cpp)
target_include_directories(litecode_codex_presentation_tests PRIVATE ${PROJECT_SOURCE_DIR}/extensions/codex)
target_link_libraries(litecode_codex_presentation_tests PRIVATE litecode_codex litecode_codex_approval LiteCode::ExtensionSDK Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.codex.presentation litecode_codex_presentation_tests)
add_executable(litecode_codex_auth_ui_tests tst_codex_auth_ui.cpp)
target_link_libraries(litecode_codex_auth_ui_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
target_compile_definitions(litecode_codex_auth_ui_tests PRIVATE
    LITECODE_CODEX_PLUGIN="$<TARGET_FILE:litecode_codex_extension>"
    LITECODE_CODEX_FIXTURE="$<TARGET_FILE:litecode_codex_runtime_fixture>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_codex_auth_ui_tests litecode_codex_extension litecode_codex_runtime_fixture litecode_extension_host)
add_litecode_qtest(litecode.codex.auth_ui litecode_codex_auth_ui_tests)
find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
add_executable(litecode_python_debugger_tests tst_python_debugger.cpp)
target_link_libraries(litecode_python_debugger_tests PRIVATE litecode_python_debugger_support litecode_ui Qt6::Test litecode_project_options)
target_compile_definitions(litecode_python_debugger_tests PRIVATE
    LITECODE_PYTHON_PACKAGE="${CMAKE_BINARY_DIR}/extension-packages/python-debugger"
    LITECODE_TEST_PYTHON="${Python3_EXECUTABLE}"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_python_debugger_tests litecode_python_debugger_extension litecode_extension_host)
add_litecode_qtest(litecode.python.debugger litecode_python_debugger_tests)
set_tests_properties(litecode.python.debugger PROPERTIES TIMEOUT 120)
add_executable(litecode_extension_install_tests tst_extension_install.cpp)
target_link_libraries(litecode_extension_install_tests PRIVATE litecode_extensions Qt6::Test litecode_project_options)
target_compile_definitions(litecode_extension_install_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_OTHER_PLUGIN="$<TARGET_FILE:litecode_other_formatter_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_extension_install_tests litecode_test_extension litecode_other_formatter_test_extension litecode_extension_host)
add_litecode_qtest(litecode.extensions.install litecode_extension_install_tests)
add_executable(litecode_extension_secret_queue_tests tst_extension_secret_queue.cpp)
target_link_libraries(litecode_extension_secret_queue_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.secret_queue litecode_extension_secret_queue_tests)
set_tests_properties(litecode.extensions.secret_queue PROPERTIES TIMEOUT 15)
add_executable(litecode_extension_output_tests tst_extension_output.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostOutput.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostMessages.cpp)
target_link_libraries(litecode_extension_output_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.output litecode_extension_output_tests)
add_executable(litecode_extension_message_tests tst_extension_messages.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostNativeViews.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostOutput.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostMessages.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDialogs.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostInputBox.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostQuickPick.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostStatusBar.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostUriHandlers.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostProgress.cpp)
target_link_libraries(litecode_extension_message_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.messages litecode_extension_message_tests)
add_executable(litecode_extension_message_ui_tests tst_extension_message_ui.cpp)
target_link_libraries(litecode_extension_message_ui_tests PRIVATE
    litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.messages_ui litecode_extension_message_ui_tests)
add_executable(litecode_extension_input_box_tests tst_extension_input_box.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostInputBox.cpp)
target_link_libraries(litecode_extension_input_box_tests PRIVATE
    litecode_ui litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.input_box litecode_extension_input_box_tests)
add_executable(litecode_extension_quick_pick_tests tst_extension_quick_pick.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostNativeViews.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostOutput.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostMessages.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDialogs.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostInputBox.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostQuickPick.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostStatusBar.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostUriHandlers.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostProgress.cpp)
target_link_libraries(litecode_extension_quick_pick_tests PRIVATE
    litecode_ui litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.quick_pick litecode_extension_quick_pick_tests)
add_executable(litecode_extension_status_bar_tests tst_extension_status_bar.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostNativeViews.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostOutput.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostMessages.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDialogs.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostInputBox.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostQuickPick.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostStatusBar.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostUriHandlers.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostProgress.cpp)
target_link_libraries(litecode_extension_status_bar_tests PRIVATE
    litecode_ui litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.status_bar litecode_extension_status_bar_tests)
add_executable(litecode_extension_document_ui_tests tst_extension_document_ui.cpp)
target_link_libraries(litecode_extension_document_ui_tests PRIVATE
    litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.documents_ui litecode_extension_document_ui_tests)
add_executable(litecode_extension_edit_review_tests tst_extension_edit_review.cpp)
target_link_libraries(litecode_extension_edit_review_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
target_compile_definitions(litecode_extension_edit_review_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_extension_edit_review_tests litecode_test_extension litecode_extension_host)
add_litecode_qtest(litecode.extensions.edit_review litecode_extension_edit_review_tests)
add_executable(litecode_document_reference_tests tst_document_references.cpp)
target_link_libraries(litecode_document_reference_tests PRIVATE litecode_editor Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.documents.references litecode_document_reference_tests)
add_executable(litecode_extension_document_opening_tests tst_extension_document_opening.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDocumentOpening.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostWorkspaceRequests.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDocuments.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostTextDocument.cpp)
target_link_libraries(litecode_extension_document_opening_tests PRIVATE
    litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.document_opening litecode_extension_document_opening_tests)
add_executable(litecode_extension_editor_edit_tests tst_extension_editor_edit.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostTextEditorEdit.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostTextDocument.cpp)
target_link_libraries(litecode_extension_editor_edit_tests PRIVATE
    litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.editor_edit litecode_extension_editor_edit_tests)
add_executable(litecode_extension_editor_edit_preparation_tests tst_extension_editor_edit_preparation.cpp)
target_link_libraries(litecode_extension_editor_edit_preparation_tests PRIVATE
    litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.editor_edit_preparation litecode_extension_editor_edit_preparation_tests)
add_executable(litecode_extension_editor_transaction_tests tst_extension_editor_transactions.cpp)
target_link_libraries(litecode_extension_editor_transaction_tests PRIVATE litecode_editor Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.editor_transactions litecode_extension_editor_transaction_tests)
add_executable(litecode_extension_editor_application_tests tst_extension_editor_application.cpp)
target_link_libraries(litecode_extension_editor_application_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.editor_application litecode_extension_editor_application_tests)
add_executable(litecode_extension_content_provider_tests tst_extension_content_providers.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostContentProviders.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDocuments.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostTextDocument.cpp)
target_link_libraries(litecode_extension_content_provider_tests PRIVATE
    litecode_extensions litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.content_providers litecode_extension_content_provider_tests)
add_executable(litecode_extension_dialog_tests tst_extension_dialogs.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDialogs.cpp)
target_link_libraries(litecode_extension_dialog_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.dialogs litecode_extension_dialog_tests)
add_executable(litecode_extension_clipboard_tests tst_extension_clipboard.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostClipboard.cpp)
target_link_libraries(litecode_extension_clipboard_tests PRIVATE
    litecode_extensions litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.clipboard litecode_extension_clipboard_tests)
add_executable(litecode_extension_environment_tests tst_extension_environment.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostClipboard.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostExternalUris.cpp)
target_link_libraries(litecode_extension_environment_tests PRIVATE
    litecode_extensions litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.environment litecode_extension_environment_tests)
add_executable(litecode_extension_uri_tests tst_extension_uris.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostExternalUris.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostUriHandlers.cpp)
target_link_libraries(litecode_extension_uri_tests PRIVATE
    litecode_extensions litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.uris litecode_extension_uri_tests)
add_executable(litecode_extension_uri_ui_tests tst_extension_uri_ui.cpp)
target_link_libraries(litecode_extension_uri_ui_tests PRIVATE litecode_ui Qt6::Concurrent Qt6::Test litecode_project_options)
target_compile_definitions(litecode_extension_uri_ui_tests PRIVATE LITECODE_TEST_PLUGIN=\"$<TARGET_FILE:litecode_test_extension>\")
target_compile_definitions(litecode_extension_uri_ui_tests PRIVATE LITECODE_EXTENSION_HOST=\"$<TARGET_FILE:litecode_extension_host>\")
add_dependencies(litecode_extension_uri_ui_tests litecode_test_extension litecode_extension_host)
add_litecode_qtest(litecode.extensions.uris_ui litecode_extension_uri_ui_tests)
add_executable(litecode_extension_window_state_tests tst_extension_window_state.cpp)
target_link_libraries(litecode_extension_window_state_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.window_state litecode_extension_window_state_tests)
foreach(_editor_host_test IN ITEMS litecode_extension_message_tests litecode_extension_quick_pick_tests
    litecode_extension_status_bar_tests litecode_extension_window_state_tests)
    target_sources(${_editor_host_test} PRIVATE
        ${PROJECT_SOURCE_DIR}/src/extensions/HostEditors.cpp
        ${PROJECT_SOURCE_DIR}/src/extensions/HostTextEditorEdit.cpp
        ${PROJECT_SOURCE_DIR}/src/extensions/HostDocuments.cpp
        ${PROJECT_SOURCE_DIR}/src/extensions/HostTextDocument.cpp
        ${PROJECT_SOURCE_DIR}/src/extensions/HostWorkspaceRequests.cpp)
endforeach()
add_executable(litecode_extension_chat_participant_tests tst_extension_chat_participant.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostChatParticipant.cpp)
target_link_libraries(litecode_extension_chat_participant_tests PRIVATE litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.chat_participant litecode_extension_chat_participant_tests)
add_executable(litecode_extension_chat_ui_tests tst_extension_chat_ui.cpp)
target_link_libraries(litecode_extension_chat_ui_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
add_dependencies(litecode_extension_chat_ui_tests litecode_test_extension litecode_extension_host)
target_compile_definitions(litecode_extension_chat_ui_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_litecode_qtest(litecode.extensions.chat_ui litecode_extension_chat_ui_tests)
add_executable(litecode_extension_package_tests tst_extension_package.cpp)
target_link_libraries(litecode_extension_package_tests PRIVATE litecode_extensions Qt6::Test litecode_project_options)
target_compile_definitions(litecode_extension_package_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>")
add_dependencies(litecode_extension_package_tests litecode_test_extension)
add_litecode_qtest(litecode.extensions.package litecode_extension_package_tests)
add_executable(litecode_extension_persistence_tests tst_extension_persistence.cpp)
target_link_libraries(litecode_extension_persistence_tests PRIVATE litecode_extensions Qt6::Test litecode_project_options)
add_dependencies(litecode_extension_persistence_tests litecode_test_extension litecode_other_formatter_test_extension litecode_extension_host)
target_compile_definitions(litecode_extension_persistence_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_OTHER_PLUGIN="$<TARGET_FILE:litecode_other_formatter_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_litecode_qtest(litecode.extensions.persistence litecode_extension_persistence_tests)
add_executable(litecode_extension_progress_tests tst_extension_progress.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostProgress.cpp)
target_link_libraries(litecode_extension_progress_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.progress litecode_extension_progress_tests)
add_executable(litecode_extension_wire_tests tst_extension_wire.cpp)
target_link_libraries(litecode_extension_wire_tests PRIVATE
    litecode_extension_wire Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.wire litecode_extension_wire_tests)
add_executable(litecode_chat_stream_ledger_tests tst_chat_stream_ledger.cpp)
target_link_libraries(litecode_chat_stream_ledger_tests PRIVATE
    litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.chat_stream litecode_chat_stream_ledger_tests)
add_executable(litecode_extension_primitives_tests tst_extension_primitives.cpp)
target_link_libraries(litecode_extension_primitives_tests PRIVATE
    LiteCode::ExtensionSDK Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions.primitives litecode_extension_primitives_tests)
set(LITECODE_PLUGIN_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(LITECODE_PLUGIN_ARCH MATCHES "^(AMD64|amd64|x86_64)$")
    set(LITECODE_PLUGIN_ARCH x86_64)
elseif(LITECODE_PLUGIN_ARCH MATCHES "^(ARM64|arm64|aarch64)$")
    set(LITECODE_PLUGIN_ARCH arm64)
endif()
configure_file(fixtures/native-test-extension.json.in native-test-extension.json @ONLY)
configure_file(fixtures/other-formatter-extension.json.in other-formatter-extension.json @ONLY)
configure_file(fixtures/conflicting-command-extension.json.in conflicting-command-extension.json @ONLY)
configure_file(fixtures/conflicting-chat-extension.json.in conflicting-chat-extension.json @ONLY)
configure_file(fixtures/default-debug-extension.json.in default-debug-extension.json @ONLY)
add_library(litecode_default_debug_test_extension MODULE fixtures/DefaultDebugExtension.cpp)
set_source_files_properties(fixtures/DefaultDebugExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/default-debug-extension.json)
set_property(TARGET litecode_default_debug_test_extension APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/default-debug-extension.json)
target_include_directories(litecode_default_debug_test_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_default_debug_test_extension PRIVATE LiteCode::ExtensionSDK)
add_library(litecode_test_extension MODULE fixtures/NativeTestExtension.cpp)
set_source_files_properties(fixtures/NativeTestExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/native-test-extension.json)
set_property(TARGET litecode_test_extension APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/native-test-extension.json)
target_include_directories(litecode_test_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_test_extension PRIVATE LiteCode::ExtensionSDK)
add_library(litecode_other_formatter_test_extension MODULE fixtures/OtherFormatterExtension.cpp)
set_source_files_properties(fixtures/OtherFormatterExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/other-formatter-extension.json)
set_property(TARGET litecode_other_formatter_test_extension APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/other-formatter-extension.json)
target_include_directories(litecode_other_formatter_test_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_other_formatter_test_extension PRIVATE LiteCode::ExtensionSDK)
add_library(litecode_conflicting_command_test_extension MODULE fixtures/ConflictingCommandExtension.cpp)
set_source_files_properties(fixtures/ConflictingCommandExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/conflicting-command-extension.json)
set_property(TARGET litecode_conflicting_command_test_extension APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/conflicting-command-extension.json)
target_include_directories(litecode_conflicting_command_test_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_conflicting_command_test_extension PRIVATE LiteCode::ExtensionSDK)
add_library(litecode_conflicting_chat_test_extension MODULE fixtures/ConflictingChatExtension.cpp)
set_source_files_properties(fixtures/ConflictingChatExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/conflicting-chat-extension.json)
set_property(TARGET litecode_conflicting_chat_test_extension APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/conflicting-chat-extension.json)
target_include_directories(litecode_conflicting_chat_test_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_conflicting_chat_test_extension PRIVATE LiteCode::ExtensionSDK)
add_dependencies(litecode_extension_tests litecode_test_extension
    litecode_other_formatter_test_extension litecode_conflicting_command_test_extension
    litecode_conflicting_chat_test_extension litecode_extension_host)
add_dependencies(litecode_extension_document_ui_tests litecode_test_extension litecode_extension_host)
add_dependencies(litecode_extension_editor_application_tests litecode_test_extension litecode_extension_host)
add_executable(litecode_extension_debug_tests tst_extension_debug.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDebug.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDebugSessions.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDebugTrackers.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDebugStarts.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostDebugConfigurations.cpp
    ${PROJECT_SOURCE_DIR}/src/extensions/HostInlineDebugAdapter.cpp)
target_link_libraries(litecode_extension_debug_tests PRIVATE litecode_ui Qt6::Network Qt6::Test litecode_project_options)
add_dependencies(litecode_extension_debug_tests litecode_test_extension litecode_extension_host litecode_dap_adapter_fixture litecode_default_debug_test_extension)
target_compile_definitions(litecode_extension_debug_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_DEFAULT_DEBUG_PLUGIN="$<TARGET_FILE:litecode_default_debug_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>"
    LITECODE_DAP_ADAPTER_FIXTURE="$<TARGET_FILE:litecode_dap_adapter_fixture>")
add_litecode_qtest(litecode.extensions.debug litecode_extension_debug_tests)
target_compile_definitions(litecode_extension_editor_application_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
target_compile_definitions(litecode_extension_document_ui_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
target_compile_definitions(litecode_extension_tests PRIVATE
    LITECODE_TEST_PLUGIN="$<TARGET_FILE:litecode_test_extension>"
    LITECODE_OTHER_FORMATTER_TEST_PLUGIN="$<TARGET_FILE:litecode_other_formatter_test_extension>"
    LITECODE_CONFLICTING_COMMAND_TEST_PLUGIN="$<TARGET_FILE:litecode_conflicting_command_test_extension>"
    LITECODE_CONFLICTING_CHAT_TEST_PLUGIN="$<TARGET_FILE:litecode_conflicting_chat_test_extension>"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
target_link_libraries(litecode_extension_tests PRIVATE litecode_extensions Qt6::Test litecode_project_options)
add_litecode_qtest(litecode.extensions litecode_extension_tests)
target_link_libraries(litecode_codex_runtime_tests PRIVATE litecode_codex Qt6::Gui Qt6::Test litecode_project_options)
target_compile_definitions(litecode_codex_runtime_tests PRIVATE
    LITECODE_CODEX_FIXTURE="$<TARGET_FILE:litecode_codex_runtime_fixture>")
add_dependencies(litecode_codex_runtime_tests litecode_codex_runtime_fixture)
add_litecode_qtest(litecode.codex.runtime litecode_codex_runtime_tests)

add_executable(litecode_workspace_tests
    tst_workspace_file_scope.cpp
)

target_link_libraries(litecode_workspace_tests
    PRIVATE
        litecode_workspace
        Qt6::Test
        litecode_project_options
)

target_compile_definitions(litecode_workspace_tests
    PRIVATE
        LITECODE_TEST_RIPGREP_PATH="${LITECODE_RIPGREP_EXECUTABLE}"
)

add_litecode_qtest(litecode.workspace.file_scope litecode_workspace_tests)

add_executable(litecode_explorer_model_tests
    tst_explorer_model.cpp
)

target_link_libraries(litecode_explorer_model_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.explorer.model litecode_explorer_model_tests)
# This test exercises file-watch notifications and destructive filesystem operations.
# Run it alone so unrelated test-process activity cannot change filesystem event timing.
set_tests_properties(litecode.explorer.model PROPERTIES RUN_SERIAL TRUE)

add_executable(litecode_editor_font_dialog_tests
    tst_editor_font_dialog.cpp
)

target_link_libraries(litecode_editor_font_dialog_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.editor.font_dialog litecode_editor_font_dialog_tests)

add_executable(litecode_settings_dialog_tests
    tst_settings_dialog.cpp
)

target_link_libraries(litecode_settings_dialog_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.settings_dialog litecode_settings_dialog_tests)

add_executable(litecode_command_registry_tests
    tst_command_registry.cpp
)

target_link_libraries(litecode_command_registry_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.command_registry litecode_command_registry_tests)

add_executable(litecode_keyboard_shortcuts_dialog_tests
    tst_keyboard_shortcuts_dialog.cpp
)

target_link_libraries(litecode_keyboard_shortcuts_dialog_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.keyboard_shortcuts litecode_keyboard_shortcuts_dialog_tests)

add_executable(litecode_quick_open_dialog_tests
    tst_quick_open_dialog.cpp
)

target_link_libraries(litecode_quick_open_dialog_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.quick_open litecode_quick_open_dialog_tests)

add_executable(litecode_language_support_tests
    tst_language_support.cpp
)

target_link_libraries(litecode_language_support_tests
    PRIVATE
        litecode_editor
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.editor.language_support litecode_language_support_tests)

add_executable(litecode_document_encoding_tests
    tst_document_encoding.cpp
)

target_link_libraries(litecode_document_encoding_tests
    PRIVATE
        litecode_editor
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.editor.document_encoding litecode_document_encoding_tests)

add_executable(litecode_editor_behavior_tests
    tst_editor_behavior.cpp
)

target_link_libraries(litecode_editor_behavior_tests
    PRIVATE
        litecode_ui
        scintilla_qt
        lexilla
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.editor_behavior litecode_editor_behavior_tests)

if(WIN32)
    add_custom_command(TARGET litecode_document_encoding_tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
                $<TARGET_FILE:Qt6::Core5Compat>
                $<TARGET_FILE_DIR:litecode_document_encoding_tests>
    )
endif()

add_executable(litecode_settings_session_tests
    tst_settings_session.cpp
)

target_link_libraries(litecode_settings_session_tests
    PRIVATE
        litecode_core
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.settings.session litecode_settings_session_tests)

add_executable(litecode_terminal_safety_tests
    tst_terminal_safety.cpp
)

if(WIN32)
    add_executable(litecode_terminal_process_helper terminal_process_helper.cpp)
    target_link_libraries(litecode_terminal_process_helper PRIVATE litecode_project_options)
    target_compile_definitions(litecode_terminal_safety_tests PRIVATE
        LITECODE_TERMINAL_PROCESS_HELPER="$<TARGET_FILE:litecode_terminal_process_helper>"
    )
    add_dependencies(litecode_terminal_safety_tests litecode_terminal_process_helper)
endif()

target_link_libraries(litecode_terminal_safety_tests
    PRIVATE
        litecode_terminal
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.terminal.safety litecode_terminal_safety_tests)

add_executable(litecode_terminal_panel_tests
    tst_terminal_panel.cpp
)

target_link_libraries(litecode_terminal_panel_tests
    PRIVATE
        litecode_ui
        Qt6::Test
        litecode_project_options
)

add_litecode_qtest(litecode.ui.terminal_panel litecode_terminal_panel_tests)

add_executable(litecode_java_javascript_debugger_tests tst_java_javascript_debuggers.cpp)
target_link_libraries(litecode_java_javascript_debugger_tests PRIVATE litecode_ui Qt6::Test litecode_project_options)
target_compile_definitions(litecode_java_javascript_debugger_tests PRIVATE
    LITECODE_JAVA_PACKAGE="${CMAKE_BINARY_DIR}/extension-packages/java-debugger"
    LITECODE_JS_PACKAGE="${CMAKE_BINARY_DIR}/extension-packages/javascript-debugger"
    LITECODE_EXTENSION_HOST="$<TARGET_FILE:litecode_extension_host>")
add_dependencies(litecode_java_javascript_debugger_tests
    litecode_java_debugger_extension litecode_javascript_debugger_extension litecode_extension_host)
add_litecode_qtest(litecode.java_javascript.debugger litecode_java_javascript_debugger_tests)
set_tests_properties(litecode.java_javascript.debugger PROPERTIES TIMEOUT 220)
