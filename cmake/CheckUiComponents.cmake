if(NOT DEFINED SOURCE_DIR)
    message(FATAL_ERROR "SOURCE_DIR is required")
endif()

file(GLOB_RECURSE ui_sources
    "${SOURCE_DIR}/src/ui/*.cpp"
    "${SOURCE_DIR}/src/ui/*.h")

set(raw_control_pattern "new[ \t\r\n]+Q(LineEdit|PushButton|ToolButton|ListWidget|Menu)[ \t\r\n]*\\(")
set(inline_style_pattern "(->|\\.)setStyleSheet[ \t\r\n]*\\(")

foreach(source IN LISTS ui_sources)
    file(TO_CMAKE_PATH "${source}" normalized_source)
    if(normalized_source MATCHES "/src/ui/components/")
        continue()
    endif()
    if(normalized_source MATCHES "/src/ui/Theme\\.cpp$")
        continue()
    endif()

    file(READ "${source}" contents)
    string(REGEX MATCH "${raw_control_pattern}" raw_control_match "${contents}")
    if(raw_control_match)
        file(RELATIVE_PATH relative_source "${SOURCE_DIR}" "${source}")
        message(FATAL_ERROR
            "${relative_source} constructs a raw Qt control (${raw_control_match}). "
            "Use a component from src/ui/components instead.")
    endif()

    string(REGEX MATCH "${inline_style_pattern}" inline_style_match "${contents}")
    if(inline_style_match)
        file(RELATIVE_PATH relative_source "${SOURCE_DIR}" "${source}")
        message(FATAL_ERROR
            "${relative_source} sets an inline stylesheet. Put visual rules in the shared "
            "component or feature stylesheet instead.")
    endif()
endforeach()
