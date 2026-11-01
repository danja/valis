# cmake/FindOrFetchHttplib.cmake
#
# cpp-httplib is a single header, so this only has to put it on the include
# path. Fetched rather than vendored so the version is visible here.

include(FetchContent)

FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib
  GIT_TAG v0.18.3
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(httplib)

if(NOT TARGET valis_httplib)
  add_library(valis_httplib INTERFACE)
  target_include_directories(valis_httplib INTERFACE ${httplib_SOURCE_DIR})
endif()

# Every translation unit that includes httplib.h must see the same macro, because
# the header's classes change layout with it. HTTPS (for Freesound) is therefore on
# for all of them when OpenSSL is present.
if(TARGET OpenSSL::SSL)
  target_compile_definitions(valis_httplib INTERFACE CPPHTTPLIB_OPENSSL_SUPPORT)
  target_link_libraries(valis_httplib INTERFACE OpenSSL::SSL OpenSSL::Crypto)
endif()
