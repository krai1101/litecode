qt_add_executable(editor_benchmark
    tst_editor_benchmark.cpp
    ../src/ui/resources/resources.qrc
)

target_link_libraries(editor_benchmark PRIVATE
    litecode_ui
    litecode_editor
    scintilla_qt
    Qt6::Test
    litecode_project_options
)

add_test(NAME litecode.benchmark.editor5mb COMMAND editor_benchmark)
set_tests_properties(litecode.benchmark.editor5mb PROPERTIES
    LABELS "benchmark"
    TIMEOUT 180
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
