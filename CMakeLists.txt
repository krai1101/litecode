cmake_minimum_required(VERSION 3.24)

project(LiteCode VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(CTest)

find_package(Qt6 6.11 REQUIRED COMPONENTS Core Core5Compat Concurrent Gui Widgets Svg)

add_library(litecode_project_options INTERFACE)
option(LITECODE_ENABLE_MSVC_ANALYZE "Enable MSVC native static analysis" OFF)
target_compile_options(litecode_project_options INTERFACE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive- /Zc:__cplusplus>
    $<$<AND:$<CXX_COMPILER_ID:MSVC>,$<BOOL:${LITECODE_ENABLE_MSVC_ANALYZE}>>:/analyze /analyze:external- /WX>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra -Wpedantic>
)

add_library(litecode_ui_primitives STATIC
    src/ui/PopupPositioner.cpp
    src/ui/PopupPositioner.h
)
target_link_libraries(litecode_ui_primitives
    PUBLIC Qt6::Widgets
    PRIVATE litecode_project_options
)
target_include_directories(litecode_ui_primitives PUBLIC ${PROJECT_SOURCE_DIR}/src)

include(cmake/Scintilla.cmake)
include(cmake/Lexilla.cmake)
include(cmake/LibVTerm.cmake)
include(cmake/Ripgrep.cmake)
include(cmake/EncodingDetector.cmake)

add_subdirectory(src/core)
add_subdirectory(src/workspace)
add_subdirectory(src/editor)
add_subdirectory(src/terminal)
add_subdirectory(src/ui)
add_subdirectory(src/app)

if(BUILD_TESTING)
    find_package(Qt6 6.11 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
    add_subdirectory(benchmarks)
endif()

set(CPACK_PACKAGE_NAME "LiteCode")
set(CPACK_PACKAGE_VENDOR "LiteCode contributors")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Lightweight native desktop IDE")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_GENERATOR "ZIP")
include(CPack)
