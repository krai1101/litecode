qt_add_executable(litecode
    main.cpp
)
target_compile_definitions(litecode PRIVATE LITECODE_VERSION="${PROJECT_VERSION}")
add_executable(litecode_git_askpass GitAskPass.cpp)
add_dependencies(litecode litecode_extension_host)
add_custom_command(TARGET litecode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:litecode_extension_host>" "$<TARGET_FILE_DIR:litecode>"
    VERBATIM)
target_link_libraries(litecode_git_askpass PRIVATE Qt6::Core Qt6::Network litecode_project_options)
set_target_properties(litecode_git_askpass PROPERTIES OUTPUT_NAME "LiteCodeGitAskPass")
add_dependencies(litecode litecode_git_askpass)
# Target ordering alone does not rerun POST_BUILD after a helper-only change.
# All supported presets use Ninja; keep its app link/staging rule dependent on
# the helper binaries so a developer launch never uses a stale host or broker.
set_property(TARGET litecode APPEND PROPERTY LINK_DEPENDS
    "$<TARGET_FILE:litecode_extension_host>"
    "$<TARGET_FILE:litecode_git_askpass>")
add_custom_command(TARGET litecode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:litecode_git_askpass>" "$<TARGET_FILE_DIR:litecode>"
    VERBATIM)
if(APPLE)
    install(TARGETS litecode_git_askpass RUNTIME DESTINATION LiteCode.app/Contents/MacOS)
else()
    install(TARGETS litecode_git_askpass RUNTIME DESTINATION bin)
endif()

if(WIN32)
    target_sources(litecode PRIVATE litecode.rc WindowsJumpList.cpp WindowsJumpList.h)
    set_property(SOURCE litecode.rc APPEND PROPERTY OBJECT_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/litecode.ico")
    target_link_libraries(litecode PRIVATE ole32 shell32 propsys uuid)
endif()

target_link_libraries(litecode PRIVATE litecode_ui litecode_project_options)

if(WIN32)
    set(_litecode_ripgrep_name rg.exe)
else()
    set(_litecode_ripgrep_name rg)
endif()

add_custom_command(TARGET litecode POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${LITECODE_RIPGREP_EXECUTABLE}" "$<TARGET_FILE_DIR:litecode>/${_litecode_ripgrep_name}"
    COMMENT "Staging bundled ripgrep beside LiteCode"
    VERBATIM
)

set_target_properties(litecode PROPERTIES
    WIN32_EXECUTABLE TRUE
    MACOSX_BUNDLE TRUE
    OUTPUT_NAME "LiteCode"
)
if(APPLE)
    target_sources(litecode PRIVATE litecode.icns)
    set_source_files_properties(litecode.icns PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    set_target_properties(litecode PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
        MACOSX_BUNDLE_ICON_FILE "litecode.icns"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    configure_file(litecode-url-handler.desktop.in litecode-url-handler.desktop @ONLY)
    install(FILES "${CMAKE_CURRENT_BINARY_DIR}/litecode-url-handler.desktop" DESTINATION share/applications)
    install(FILES "${PROJECT_SOURCE_DIR}/src/ui/resources/icons/litecode.svg"
        DESTINATION share/icons/hicolor/scalable/apps)
elseif(WIN32)
    install(FILES "${PROJECT_SOURCE_DIR}/scripts/register-uri-handler.ps1" DESTINATION bin)
endif()

if(WIN32)
    add_custom_command(TARGET litecode POST_BUILD
        COMMAND $<TARGET_FILE:Qt6::windeployqt>
            --no-translations
            --no-compiler-runtime
            --no-system-d3d-compiler
            --no-opengl-sw
            --dir $<TARGET_FILE_DIR:litecode>
            $<TARGET_FILE:litecode>
        COMMENT "Deploying the Qt runtime beside LiteCode"
        VERBATIM
    )
endif()

install(TARGETS litecode
    BUNDLE DESTINATION .
    RUNTIME DESTINATION bin
)

# The native host has dependencies that the workbench may not import (for
# example keychain/crypto and Qt DBus on Unix). Scan all shipped helpers too.
if(APPLE)
    set(_litecode_deploy_executable "LiteCode.app")
    set(_litecode_deploy_helpers
        "LiteCode.app/Contents/MacOS/$<TARGET_FILE_NAME:litecode_extension_host>"
        "LiteCode.app/Contents/MacOS/$<TARGET_FILE_NAME:litecode_git_askpass>")
    set_target_properties(litecode_extension_host litecode_git_askpass PROPERTIES
        INSTALL_RPATH "@executable_path/../Frameworks")
else()
    set(_litecode_deploy_executable "$<TARGET_FILE:litecode>")
    set(_litecode_deploy_helpers "$<TARGET_FILE:litecode_extension_host>" "$<TARGET_FILE:litecode_git_askpass>")
    if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
        set_target_properties(litecode litecode_extension_host litecode_git_askpass PROPERTIES
            INSTALL_RPATH "$ORIGIN/../lib")
    endif()
endif()
set(_litecode_deploy_helper_args "")
foreach(_litecode_helper IN LISTS _litecode_deploy_helpers)
    string(APPEND _litecode_deploy_helper_args " \"${_litecode_helper}\"")
endforeach()
qt_generate_deploy_script(TARGET litecode OUTPUT_SCRIPT litecode_deploy_script
    CONTENT "qt_deploy_runtime_dependencies(
        EXECUTABLE \"${_litecode_deploy_executable}\"
        ADDITIONAL_EXECUTABLES ${_litecode_deploy_helper_args}
        GENERATE_QT_CONF NO_TRANSLATIONS NO_COMPILER_RUNTIME)")
install(SCRIPT ${litecode_deploy_script})
install(FILES ${PROJECT_SOURCE_DIR}/LICENSE ${PROJECT_SOURCE_DIR}/NOTICE.md
              ${PROJECT_SOURCE_DIR}/THIRD_PARTY.md ${PROJECT_SOURCE_DIR}/SBOM.spdx.json
        DESTINATION .)
install(FILES ${PROJECT_SOURCE_DIR}/licenses/LGPL-3.0.txt
              ${PROJECT_SOURCE_DIR}/licenses/GPL-3.0.txt
              ${PROJECT_SOURCE_DIR}/licenses/SCINTILLA.txt
              ${PROJECT_SOURCE_DIR}/licenses/LIBVTERM-MIT.txt
              ${PROJECT_SOURCE_DIR}/licenses/CODICONS-CC-BY-4.0.txt
              ${PROJECT_SOURCE_DIR}/licenses/SETI-UI-MIT.txt
              ${PROJECT_SOURCE_DIR}/licenses/VSCODE-MIT.txt
              ${PROJECT_SOURCE_DIR}/licenses/LUCIDE-ISC.txt
              ${PROJECT_SOURCE_DIR}/licenses/RIPGREP-MIT.txt
              ${PROJECT_SOURCE_DIR}/licenses/CODEX-APACHE-2.0.txt
              ${PROJECT_SOURCE_DIR}/licenses/CODEX-NOTICE.txt
              ${PROJECT_SOURCE_DIR}/licenses/CODEX-ZSH.txt
        DESTINATION licenses)
install(FILES ${uchardet_source_SOURCE_DIR}/COPYING
        DESTINATION licenses
        RENAME UCHARDET-MPL-1.1.txt)

if(APPLE)
    install(PROGRAMS ${LITECODE_RIPGREP_EXECUTABLE} DESTINATION LiteCode.app/Contents/MacOS)
else()
    install(PROGRAMS ${LITECODE_RIPGREP_EXECUTABLE} DESTINATION bin)
endif()
