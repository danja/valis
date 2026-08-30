# cmake/FindOrFetchSerd.cmake
#
# Provides the imported targets valis::serd and valis::sord.
#
# serd and sord build with meson, not CMake, so FetchContent + add_subdirectory
# cannot work. System packages are preferred; otherwise the sources are fetched
# and compiled by the hand-written shims below. Both projects check in their
# *_config.h, so they compile with no meson-generated header.

include(FetchContent)
find_package(PkgConfig QUIET)

set(_valis_serd_system FALSE)
if(PkgConfig_FOUND)
  pkg_check_modules(SERD QUIET IMPORTED_TARGET serd-0)
  pkg_check_modules(SORD QUIET IMPORTED_TARGET sord-0)
  # The SerdError struct layout changed in 0.32.0; reject older system packages
  # to avoid a runtime SEGFAULT in the error callback.
  if(SERD_FOUND AND SORD_FOUND AND SERD_VERSION VERSION_GREATER_EQUAL "0.32.0")
    set(_valis_serd_system TRUE)
  endif()
endif()

if(_valis_serd_system)
  message(STATUS "valis: using system serd ${SERD_VERSION} / sord ${SORD_VERSION}")
  add_library(valis_serd INTERFACE)
  add_library(valis_sord INTERFACE)
  target_link_libraries(valis_serd INTERFACE PkgConfig::SERD)
  target_link_libraries(valis_sord INTERFACE PkgConfig::SORD)
else()
  message(STATUS "valis: system serd/sord not found, fetching sources")

  FetchContent_Declare(serd
    GIT_REPOSITORY https://github.com/drobilla/serd
    GIT_TAG v0.32.10
    GIT_SHALLOW TRUE)
  FetchContent_Declare(sord
    GIT_REPOSITORY https://github.com/drobilla/sord
    GIT_TAG v0.16.8
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(serd sord)

  # serdi.c is the command-line tool's main(), not library code.
  add_library(valis_serd STATIC
    ${serd_SOURCE_DIR}/src/base64.c
    ${serd_SOURCE_DIR}/src/byte_source.c
    ${serd_SOURCE_DIR}/src/env.c
    ${serd_SOURCE_DIR}/src/n3.c
    ${serd_SOURCE_DIR}/src/node.c
    ${serd_SOURCE_DIR}/src/read_utf8.c
    ${serd_SOURCE_DIR}/src/reader.c
    ${serd_SOURCE_DIR}/src/string.c
    ${serd_SOURCE_DIR}/src/system.c
    ${serd_SOURCE_DIR}/src/uri.c
    ${serd_SOURCE_DIR}/src/writer.c)
  target_include_directories(valis_serd
    PUBLIC  ${serd_SOURCE_DIR}/include
    PRIVATE ${serd_SOURCE_DIR}/src)
  # Only meaningful on Windows (dllimport/dllexport). Defining it unconditionally
  # collides with JUCE's own bundled serd, which #defines it in a header.
  target_compile_definitions(valis_serd PUBLIC $<$<PLATFORM_ID:Windows>:SERD_STATIC>)

  # sord.c #includes zix/btree.c, zix/digest.c and zix/hash.c directly, so those
  # must not be listed here as well. sordi.c and sord_validate.c are tool mains.
  add_library(valis_sord STATIC
    ${sord_SOURCE_DIR}/src/sord.c
    ${sord_SOURCE_DIR}/src/syntax.c)
  target_include_directories(valis_sord
    PUBLIC  ${sord_SOURCE_DIR}/include
    PRIVATE ${sord_SOURCE_DIR}/src)
  target_compile_definitions(valis_sord PUBLIC $<$<PLATFORM_ID:Windows>:SORD_STATIC>)
  target_link_libraries(valis_sord PUBLIC valis_serd)

  foreach(_t valis_serd valis_sord)
    set_target_properties(${_t} PROPERTIES POSITION_INDEPENDENT_CODE ON)
    # Vendored third-party code: do not hold it to our warning settings.
    target_compile_options(${_t} PRIVATE -w)
  endforeach()
endif()

add_library(valis::serd ALIAS valis_serd)
add_library(valis::sord ALIAS valis_sord)
