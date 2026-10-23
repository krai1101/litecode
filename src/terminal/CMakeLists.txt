add_library(litecode_terminal STATIC
    ITerminalBackend.h
    TerminalFactory.cpp
    TerminalFactory.h
    TerminalInputQueue.cpp
    TerminalInputQueue.h
    TerminalTypes.cpp
    TerminalTypes.h
    TerminalSessionService.cpp
    TerminalSessionService.h
    VTermEngine.cpp
    VTermEngine.h
)

if(WIN32)
    target_sources(litecode_terminal PRIVATE WindowsConPtyBackend.cpp WindowsConPtyBackend.h)
    target_compile_definitions(litecode_terminal PRIVATE _WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX)
else()
    target_sources(litecode_terminal PRIVATE UnixPtyBackend.cpp UnixPtyBackend.h)
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        target_link_libraries(litecode_terminal PRIVATE util)
    endif()
endif()

target_link_libraries(litecode_terminal
    PUBLIC
        litecode_core
        Qt6::Core
    PRIVATE
        litecode_libvterm
        litecode_project_options
)
target_include_directories(litecode_terminal PUBLIC ${PROJECT_SOURCE_DIR}/src)
