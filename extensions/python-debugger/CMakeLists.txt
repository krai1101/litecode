include(cmake/Debugpy.cmake)
set(_python_package "${CMAKE_BINARY_DIR}/extension-packages/python-debugger")
set(PYTHON_DEBUGGER_ARCH "${CMAKE_SYSTEM_PROCESSOR}")
if(PYTHON_DEBUGGER_ARCH MATCHES "AMD64|amd64|x86_64")
    set(PYTHON_DEBUGGER_ARCH x86_64)
elseif(PYTHON_DEBUGGER_ARCH MATCHES "ARM64|arm64|aarch64")
    set(PYTHON_DEBUGGER_ARCH arm64)
endif()
configure_file(extension.json.in extension.json @ONLY)
add_library(litecode_python_debugger_support STATIC
    PythonProcess.cpp PythonProcess.h PythonInterpreters.cpp PythonInterpreters.h
    PythonConfiguration.cpp PythonConfiguration.h PythonEnvironment.cpp PythonEnvironment.h
    PythonDebugProvider.cpp PythonDebugProvider.h)
set_target_properties(litecode_python_debugger_support PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_include_directories(litecode_python_debugger_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(litecode_python_debugger_support PUBLIC LiteCode::ExtensionSDK PRIVATE litecode_project_options)
add_library(litecode_python_debugger_extension MODULE PythonDebuggerExtension.cpp)
target_include_directories(litecode_python_debugger_extension PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_link_libraries(litecode_python_debugger_extension PRIVATE litecode_python_debugger_support litecode_project_options)
set_source_files_properties(PythonDebuggerExtension.cpp PROPERTIES OBJECT_DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/extension.json")
set_target_properties(litecode_python_debugger_extension PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${_python_package}" RUNTIME_OUTPUT_DIRECTORY "${_python_package}")
file(MAKE_DIRECTORY "${_python_package}/bundled/libs")
file(COPY "${LITECODE_DEBUGPY_DIRECTORY}" DESTINATION "${_python_package}/bundled/libs")
file(COPY "${python_debugger_runtime_SOURCE_DIR}/" DESTINATION "${_python_package}/licenses"
    FILES_MATCHING PATTERN "LICENSE*" PATTERN "COPYING*" PATTERN "NOTICE*")
configure_file(README.md "${_python_package}/README.md" COPYONLY)
configure_file(${PROJECT_SOURCE_DIR}/LICENSE "${_python_package}/LICENSE" COPYONLY)
configure_file(assets/python-debugger.png "${_python_package}/python-debugger.png" COPYONLY)
install(DIRECTORY "${_python_package}/" DESTINATION extensions/python-debugger USE_SOURCE_PERMISSIONS)

configure_file(assets/VSCODE-PYTHON-DEBUGGER-MIT.txt "${_python_package}/licenses/VSCODE-PYTHON-DEBUGGER-MIT.txt" COPYONLY)

configure_file(assets/ATTRIBUTION.md "${_python_package}/licenses/ATTRIBUTION.md" COPYONLY)
