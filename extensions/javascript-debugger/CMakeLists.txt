include(FetchContent)
FetchContent_Declare(js_debug_runtime
    URL https://github.com/microsoft/vscode-js-debug/releases/download/v1.140.1/js-debug-dap-v1.140.1.tar.gz
    URL_HASH SHA256=c72fab31bcbfdce6ad1f42adfd28eff8d4681dd3b03b5e2e6f9e72e6f9927faf
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR _no_cmake_project)
FetchContent_MakeAvailable(js_debug_runtime)
set(package "${CMAKE_BINARY_DIR}/extension-packages/javascript-debugger")
configure_file(javascript-debugger.svg "${package}/javascript-debugger.svg" COPYONLY)
file(COPY "${js_debug_runtime_SOURCE_DIR}/" DESTINATION "${package}/bundled/js-debug")

set(DEBUGGER_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(DEBUGGER_ARCH MATCHES "AMD64|amd64|x86_64")
    set(DEBUGGER_ARCH x86_64)
elseif(DEBUGGER_ARCH MATCHES "ARM64|arm64|aarch64")
    set(DEBUGGER_ARCH arm64)
endif()
configure_file(extension.json.in extension.json @ONLY)
add_library(litecode_javascript_debugger_extension MODULE
    JavaScriptDebuggerExtension.cpp JsAdapter.cpp JsAdapter.h)
target_include_directories(litecode_javascript_debugger_extension PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(litecode_javascript_debugger_extension PRIVATE
    LiteCode::ExtensionSDK litecode_debug Qt6::Network litecode_project_options)
set_target_properties(litecode_javascript_debugger_extension PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${package}" RUNTIME_OUTPUT_DIRECTORY "${package}")
configure_file(README.md "${package}/README.md" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${package}/LICENSE" COPYONLY)
install(DIRECTORY "${package}/" DESTINATION extensions/javascript-debugger USE_SOURCE_PERMISSIONS)
