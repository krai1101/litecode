include(FetchContent)
FetchContent_Declare(java_debug_runtime
    URL https://open-vsx.org/api/vscjava/vscode-java-debug/0.60.0/file/vscjava.vscode-java-debug-0.60.0.vsix
    DOWNLOAD_NAME java-debug-runtime.zip
    URL_HASH SHA256=f09ebe358a7b08b3c17b21b8d8207b0649acad120eb6f1604dc4f15379f59d66
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR _no_cmake_project)
FetchContent_Declare(jdtls_runtime
    URL https://download.eclipse.org/jdtls/milestones/1.57.0/jdt-language-server-1.57.0-202602261110.tar.gz
    URL_HASH SHA256=f7ffa93fe1bbbea95dac13dd97cdcd25c582d6e56db67258da0dcceb2302601e
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE SOURCE_SUBDIR _no_cmake_project)
FetchContent_MakeAvailable(java_debug_runtime jdtls_runtime)
set(package "${CMAKE_BINARY_DIR}/extension-packages/java-debugger")
file(COPY "${jdtls_runtime_SOURCE_DIR}/" DESTINATION "${package}/bundled/jdtls")
file(COPY "${java_debug_runtime_SOURCE_DIR}/extension/server/com.microsoft.java.debug.plugin-0.53.2.jar"
    DESTINATION "${package}/bundled")
file(COPY "${java_debug_runtime_SOURCE_DIR}/extension/LICENSE.txt"
    "${java_debug_runtime_SOURCE_DIR}/extension/ThirdPartyNotices.txt" DESTINATION "${package}/licenses/java-debug")
configure_file(JAVA-DEBUG-LICENSE.txt "${package}/licenses/java-debug/JAVA-DEBUG-LICENSE.txt" COPYONLY)

set(DEBUGGER_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(DEBUGGER_ARCH MATCHES "AMD64|amd64|x86_64")
    set(DEBUGGER_ARCH x86_64)
elseif(DEBUGGER_ARCH MATCHES "ARM64|arm64|aarch64")
    set(DEBUGGER_ARCH arm64)
endif()
configure_file(extension.json.in extension.json @ONLY)
add_library(litecode_java_debugger_extension MODULE
    JavaDebuggerExtension.cpp JavaLanguageServer.cpp JavaLanguageServer.h)
target_include_directories(litecode_java_debugger_extension PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(litecode_java_debugger_extension PRIVATE
    LiteCode::ExtensionSDK litecode_debug Qt6::Network litecode_project_options)
set_target_properties(litecode_java_debugger_extension PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${package}" RUNTIME_OUTPUT_DIRECTORY "${package}")
configure_file(README.md "${package}/README.md" COPYONLY)
configure_file("${PROJECT_SOURCE_DIR}/LICENSE" "${package}/LICENSE" COPYONLY)
install(DIRECTORY "${package}/" DESTINATION extensions/java-debugger USE_SOURCE_PERMISSIONS)
