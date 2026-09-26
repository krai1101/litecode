include(FetchContent)

FetchContent_Declare(lexilla_source
    # Use the upstream GitHub tag, which is more reliable for CI than the project download site.
    URL https://github.com/ScintillaOrg/lexilla/archive/refs/tags/rel-5-5-1.zip
    URL_HASH SHA256=5c147f927927b35c55c630ef3a9affd5708400a3cad80447cc1043ab9a816f31
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(lexilla_source)

set(_lexilla_root "${lexilla_source_SOURCE_DIR}")
# Lexilla is fetched at an exact, hash-verified version. Its source set cannot
# change during a normal build, so resolve it once at configure time instead of
# running CMake's glob verifier before every Ninja invocation.
file(GLOB _lexilla_lexers "${_lexilla_root}/lexers/*.cxx")
file(GLOB _lexilla_support "${_lexilla_root}/lexlib/*.cxx")

add_library(lexilla STATIC
    ${_lexilla_root}/src/Lexilla.cxx
    ${_lexilla_lexers}
    ${_lexilla_support}
)
target_include_directories(lexilla
    PUBLIC ${_lexilla_root}/include
    PRIVATE
        ${_lexilla_root}/lexlib
        ${scintilla_source_SOURCE_DIR}/include
)
set_target_properties(lexilla PROPERTIES POSITION_INDEPENDENT_CODE ON)
