add_library(litecode_debug STATIC DapProtocol.h DapProtocol.cpp DapTransport.h DapTransport.cpp
    DapSocketConnection.h DapSocketConnection.cpp
    DebugSession.h DebugSession.cpp DebugConfiguration.h DebugConfiguration.cpp
    TaskConfiguration.h TaskConfiguration.cpp TaskRunner.h TaskRunner.cpp)
target_include_directories(litecode_debug PUBLIC ${PROJECT_SOURCE_DIR}/src)
target_link_libraries(litecode_debug PUBLIC Qt6::Core PRIVATE Qt6::Network litecode_jsonc litecode_project_options)
