include(FetchContent)

# Ripgrep is shipped as an unmodified executable for the three release targets.
# Keeping this here makes the downloaded archive, hash, and installed binary part
# of the normal CMake build instead of relying on a tool found on the user's PATH.
set(LITECODE_RIPGREP_VERSION "15.2.0")

if(WIN32)
    set(_ripgrep_archive "ripgrep-15.2.0-x86_64-pc-windows-msvc.zip")
    set(_ripgrep_hash "71b2fef860abe467217a538ff31de02f5258807c0129f771846f87bd029aafc5")
    set(_ripgrep_binary "rg.exe")
elseif(APPLE)
    set(_ripgrep_macos_arch "${CMAKE_OSX_ARCHITECTURES}")
    if(NOT _ripgrep_macos_arch)
        set(_ripgrep_macos_arch "${CMAKE_SYSTEM_PROCESSOR}")
    endif()
    if(_ripgrep_macos_arch MATCHES "arm64|aarch64")
        set(_ripgrep_archive "ripgrep-15.2.0-aarch64-apple-darwin.tar.gz")
        set(_ripgrep_hash "3750b2e93f37e0c692657da574d7019a101c0084da05a790c83fd335bad973e4")
    elseif(_ripgrep_macos_arch MATCHES "x86_64|amd64")
        set(_ripgrep_archive "ripgrep-15.2.0-x86_64-apple-darwin.tar.gz")
        set(_ripgrep_hash "af7825fcc69a2afc7a7aea55fc9af90e26421d8f20fe59df32e233c0b8a231c1")
    else()
        message(FATAL_ERROR "LiteCode has no bundled ripgrep archive for macOS architecture: ${_ripgrep_macos_arch}")
    endif()
    set(_ripgrep_binary "rg")
elseif(UNIX)
    set(_ripgrep_archive "ripgrep-15.2.0-x86_64-unknown-linux-musl.tar.gz")
    set(_ripgrep_hash "33e15bcf1624b25cdd2a55813a47a2f95dbe126268203e76aa6a585d1e7b149c")
    set(_ripgrep_binary "rg")
else()
    message(FATAL_ERROR "LiteCode does not have a bundled ripgrep archive for this platform.")
endif()

FetchContent_Declare(ripgrep_runtime
    URL "https://github.com/BurntSushi/ripgrep/releases/download/${LITECODE_RIPGREP_VERSION}/${_ripgrep_archive}"
    URL_HASH "SHA256=${_ripgrep_hash}"
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(ripgrep_runtime)

# FetchContent strips the archive's single top-level directory while extracting.
set(LITECODE_RIPGREP_EXECUTABLE "${ripgrep_runtime_SOURCE_DIR}/${_ripgrep_binary}")

if(NOT EXISTS "${LITECODE_RIPGREP_EXECUTABLE}")
    message(FATAL_ERROR "Bundled ripgrep executable was not found after extraction: ${LITECODE_RIPGREP_EXECUTABLE}")
endif()
