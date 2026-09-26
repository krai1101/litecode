include(FetchContent)

# libvterm is the terminal state machine only. LiteCode retains its native
# ConPTY/POSIX PTY backends and renders the resulting cell grid with Qt.
FetchContent_Declare(libvterm_source
    URL https://github.com/neovim/libvterm/archive/refs/tags/v0.3.3.tar.gz
    URL_HASH SHA256=0babe3ab42c354925dadedE90d352f054aa9c4ae6842ea803a20c9741e172e56
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(libvterm_source)

# Release archives keep the source tables but not the two generated C
# includes. Ship deterministic outputs of upstream's tbl2inc_c.pl so building
# LiteCode does not add Perl as a toolchain dependency.
file(COPY ${CMAKE_CURRENT_LIST_DIR}/libvterm/DECdrawing.inc
          ${CMAKE_CURRENT_LIST_DIR}/libvterm/uk.inc
     DESTINATION ${libvterm_source_SOURCE_DIR}/src/encoding)

add_library(libvterm STATIC
    ${libvterm_source_SOURCE_DIR}/src/encoding.c
    ${libvterm_source_SOURCE_DIR}/src/keyboard.c
    ${libvterm_source_SOURCE_DIR}/src/mouse.c
    ${libvterm_source_SOURCE_DIR}/src/parser.c
    ${libvterm_source_SOURCE_DIR}/src/pen.c
    ${libvterm_source_SOURCE_DIR}/src/screen.c
    ${libvterm_source_SOURCE_DIR}/src/state.c
    ${libvterm_source_SOURCE_DIR}/src/unicode.c
    ${libvterm_source_SOURCE_DIR}/src/vterm.c
)
target_include_directories(libvterm PUBLIC ${libvterm_source_SOURCE_DIR}/include)
target_include_directories(libvterm PRIVATE ${libvterm_source_SOURCE_DIR}/src)
target_compile_definitions(libvterm PRIVATE _CRT_SECURE_NO_WARNINGS)
set_target_properties(libvterm PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    POSITION_INDEPENDENT_CODE ON
)
add_library(litecode_libvterm ALIAS libvterm)
