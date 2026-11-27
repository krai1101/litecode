include(FetchContent)

# Keep credential-service plumbing private to the native host. Prefer an
# installed Qt6Keychain; otherwise build pinned source against our Qt.
function(litecode_link_secret_storage target)
    find_package(OpenSSL 3.0 REQUIRED COMPONENTS Crypto)
    find_package(Qt6Keychain 0.14 CONFIG QUIET)
    if(Qt6Keychain_FOUND)
        target_link_libraries(${target} PRIVATE qt6keychain OpenSSL::Crypto)
        return()
    endif()

    set(BUILD_WITH_QT6 ON)
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_TRANSLATIONS OFF)
    set(BUILD_TEST_APPLICATION OFF)
    set(BUILD_TESTING OFF)
    set(LIBSECRET_SUPPORT ON)
    FetchContent_Declare(litecode_qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG ad7344c45a86a4f66cbafc4b081b5f7b876cb0b7
        GIT_PROGRESS FALSE
    )
    FetchContent_MakeAvailable(litecode_qtkeychain)
    target_compile_definitions(${target} PRIVATE LITECODE_BUNDLED_QTKEYCHAIN)
    target_link_libraries(${target} PRIVATE qt6keychain OpenSSL::Crypto)
    install(FILES ${litecode_qtkeychain_SOURCE_DIR}/COPYING
        DESTINATION licenses RENAME QTKEYCHAIN-BSD.txt)
endfunction()
