include(FetchContent)
# debugpy is extension-owned, never installed into the user's Python environment.
# Its Python implementation works on all supported desktop platforms; Windows
# wheels also supply native attach helpers. Compiled tracing speedups are optional.
set(LITECODE_DEBUGPY_VERSION 1.8.22)
if(WIN32 AND CMAKE_SYSTEM_PROCESSOR MATCHES "AMD64|amd64|x86_64")
    set(_debugpy_url "https://files.pythonhosted.org/packages/82/7f/633160132b80b424c6816391f239682435c6b77cdd58288d04b3443b9f80/debugpy-1.8.22-cp310-cp310-win_amd64.whl")
    set(_debugpy_hash 1bd0c6df3c68c0a3f71db8baa3780a953abb65537ec4b3bc6b935ad5b3b3d45c)
    set(_debugpy_suffix "")
    set(_debugpy_download_name debugpy-runtime.zip)
else()
    set(_debugpy_url "https://files.pythonhosted.org/packages/44/9d/3cb6693342acf96802dba89934a5b8da43c201d764ebd1f2c0514a3d42bd/debugpy-1.8.22.tar.gz")
    set(_debugpy_hash e489c7268e1c7b41e13b438d9c533d2a7af73fb59bf8cd30fead8286c1c39c4e)
    set(_debugpy_suffix "/src")
    set(_debugpy_download_name debugpy-runtime.tar.gz)
endif()
FetchContent_Declare(python_debugger_runtime URL "${_debugpy_url}"
    DOWNLOAD_NAME "${_debugpy_download_name}"
    URL_HASH "SHA256=${_debugpy_hash}" DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _no_cmake_project)
FetchContent_MakeAvailable(python_debugger_runtime)
set(LITECODE_DEBUGPY_DIRECTORY "${python_debugger_runtime_SOURCE_DIR}${_debugpy_suffix}/debugpy")
if(NOT EXISTS "${LITECODE_DEBUGPY_DIRECTORY}/adapter/__main__.py")
    message(FATAL_ERROR "The pinned debugpy package has an unexpected layout")
endif()
