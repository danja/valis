# cmake/ValisDeps.cmake
#
# valis_make_available(name...) is FetchContent_MakeAvailable with a local cache. The first configure on a machine fetches
# each dependency and copies its source to ${VALIS_DEPS_DIR}/<name> (default: deps/ in the repository, which is
# git-ignored); every later configure, in any build directory, uses that copy and needs no network. Delete a directory
# in deps/ to fetch it again, for instance after changing its pinned version in cmake/FindOrFetch*.cmake.
include(FetchContent)

set(VALIS_DEPS_DIR "${CMAKE_SOURCE_DIR}/deps" CACHE PATH "Where fetched dependencies are kept between builds")

macro(valis_make_available)
  foreach(_vd_name ${ARGN})
    string(TOUPPER "${_vd_name}" _vd_upper)
    if(EXISTS "${VALIS_DEPS_DIR}/${_vd_name}/.valis-cached")
      set(FETCHCONTENT_SOURCE_DIR_${_vd_upper} "${VALIS_DEPS_DIR}/${_vd_name}")
      message(STATUS "valis: ${_vd_name} from ${VALIS_DEPS_DIR}/${_vd_name}")
    endif()
  endforeach()
  FetchContent_MakeAvailable(${ARGN})
  foreach(_vd_name ${ARGN})
    if(NOT EXISTS "${VALIS_DEPS_DIR}/${_vd_name}/.valis-cached")
      FetchContent_GetProperties(${_vd_name} SOURCE_DIR _vd_source)
      file(MAKE_DIRECTORY "${VALIS_DEPS_DIR}/${_vd_name}")
      file(COPY "${_vd_source}/" DESTINATION "${VALIS_DEPS_DIR}/${_vd_name}" PATTERN ".git" EXCLUDE)
      file(WRITE "${VALIS_DEPS_DIR}/${_vd_name}/.valis-cached" "fetched by cmake\n")
      message(STATUS "valis: kept a copy of ${_vd_name} in ${VALIS_DEPS_DIR}/${_vd_name}")
    endif()
  endforeach()
endmacro()
