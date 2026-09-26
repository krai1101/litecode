include(FetchContent)

# Native C++ detector, pinned and hash-verified for reproducible builds.
FetchContent_Declare(uchardet_source
    URL https://deb.debian.org/debian/pool/main/u/uchardet/uchardet_0.0.8.orig.tar.xz
    URL_HASH SHA256=e97a60cfc00a1c147a674b097bb1422abd9fa78a2d9ce3f3fdcc2e78a34ac5f0
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    SOURCE_SUBDIR _litecode_population_only
)
FetchContent_MakeAvailable(uchardet_source)

file(GLOB UCHARDET_SOURCES CONFIGURE_DEPENDS
    ${uchardet_source_SOURCE_DIR}/src/*.cpp
    ${uchardet_source_SOURCE_DIR}/src/LangModels/*.cpp
)
add_library(litecode_uchardet STATIC ${UCHARDET_SOURCES})
target_include_directories(litecode_uchardet PUBLIC ${uchardet_source_SOURCE_DIR}/src)
target_compile_definitions(litecode_uchardet PRIVATE BUILDING_UCHARDET VERSION="0.0.8")
target_compile_options(litecode_uchardet PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/w>
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-w>
)
set_target_properties(litecode_uchardet PROPERTIES AUTOMOC OFF)
