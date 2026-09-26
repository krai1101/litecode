function(add_litecode_qtest test_name target)
    if(WIN32)
        # GUI Qt tests have no console output on Windows; save their result for CI diagnostics.
        add_test(NAME ${test_name} COMMAND ${target} -o ${test_name}.qtest.txt,txt)
    else()
        add_test(NAME ${test_name} COMMAND ${target})
    endif()
    set_tests_properties(${test_name} PROPERTIES TIMEOUT 120)
endfunction()

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
