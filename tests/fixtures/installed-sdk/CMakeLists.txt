cmake_minimum_required(VERSION 3.24)
project(LiteCodeHelloExtension VERSION 1.0.0 LANGUAGES CXX)
set(CMAKE_AUTOMOC ON)
find_package(LiteCodeExtensionSDK 16.0.0 EXACT REQUIRED CONFIG)

set(EXTENSION_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(EXTENSION_ARCH MATCHES "^(AMD64|amd64|x86_64)$")
    set(EXTENSION_ARCH x86_64)
elseif(EXTENSION_ARCH MATCHES "^(ARM64|arm64|aarch64)$")
    set(EXTENSION_ARCH arm64)
endif()
configure_file(extension.json.in extension.json @ONLY)
add_library(litecode_hello MODULE HelloExtension.cpp)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/native-extension-path-$<CONFIG>.txt"
    CONTENT "$<TARGET_FILE:litecode_hello>\n")
set_source_files_properties(HelloExtension.cpp PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/extension.json)
set_property(TARGET litecode_hello APPEND PROPERTY AUTOGEN_TARGET_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/extension.json)
target_include_directories(litecode_hello PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_hello PRIVATE LiteCode::ExtensionSDK)
