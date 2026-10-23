add_library(litecode_core STATIC
    DocumentTypes.cpp
    DocumentTypes.h
    FileSystemPath.cpp
    FileSystemPath.h
    OperationResult.cpp
    OperationResult.h
    ProcessLifetime.cpp
    ProcessLifetime.h
    SettingsService.cpp
    SettingsService.h
    TextEncoding.cpp
    TextEncoding.h
)

target_link_libraries(litecode_core
    PUBLIC
        Qt6::Core
        Qt6::Core5Compat
    PRIVATE
        litecode_uchardet
        litecode_project_options
)
target_include_directories(litecode_core PUBLIC ${PROJECT_SOURCE_DIR}/src)
