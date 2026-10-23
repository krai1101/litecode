add_library(litecode_editor STATIC
    DocumentSession.cpp
    DocumentSession.h
    EditorBackend.h
    LanguageSupport.cpp
    LanguageSupport.h
    LanguageStyleCatalog.cpp
    LanguageStyleCatalog.h
    ScintillaEditor.cpp
    ScintillaEditor.h
    SyntaxTheme.cpp
    SyntaxTheme.h
    resources/themes/LiteCodeThemes.qrc
)

target_link_libraries(litecode_editor
    PUBLIC
        litecode_core
        Qt6::Core
        Qt6::Widgets
    PRIVATE
        Qt6::Concurrent
        scintilla_qt
        lexilla
        litecode_project_options
)
target_include_directories(litecode_editor PUBLIC ${PROJECT_SOURCE_DIR}/src)
