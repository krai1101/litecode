add_library(litecode_workspace STATIC
    WorkspaceService.cpp
    WorkspaceService.h
    WorkspaceSearch.cpp
    WorkspaceSearch.h
    WorkspaceReplace.cpp
    WorkspaceReplace.h
    WorkspaceFileScope.cpp
    WorkspaceFileScope.h
    WorkspaceTextQuery.cpp
    WorkspaceTextQuery.h
)

target_link_libraries(litecode_workspace
    PUBLIC
        litecode_core
        Qt6::Core
    PRIVATE
        Qt6::Concurrent
        litecode_project_options
)
target_include_directories(litecode_workspace PUBLIC ${PROJECT_SOURCE_DIR}/src)
