qt_add_executable(litecode
    main.cpp
)

if(WIN32)
    target_sources(litecode PRIVATE litecode.rc)
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

qt_generate_deploy_app_script(
    TARGET litecode
    OUTPUT_SCRIPT litecode_deploy_script
    NO_TRANSLATIONS
    NO_COMPILER_RUNTIME
    NO_UNSUPPORTED_PLATFORM_ERROR
)
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
        DESTINATION licenses)
install(FILES ${uchardet_source_SOURCE_DIR}/COPYING
        DESTINATION licenses
        RENAME UCHARDET-MPL-1.1.txt)

if(APPLE)
    install(PROGRAMS ${LITECODE_RIPGREP_EXECUTABLE} DESTINATION LiteCode.app/Contents/MacOS)
else()
    install(PROGRAMS ${LITECODE_RIPGREP_EXECUTABLE} DESTINATION bin)
endif()
