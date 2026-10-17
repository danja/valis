# cmake/FindOrFetchNam.cmake
#
# Neural Amp Modeler Core is built from source into a static library rather than
# through its own CMakeLists, which is a project of tools and not a library. Its
# Eigen dependency is a git submodule that is often not checked out, so Eigen is
# taken from the system if present and fetched otherwise. Eigen is header-only.

set(VALIS_NAM_DIR "/home/danny/github/NeuralAmpModelerCore" CACHE PATH
    "Path to the NeuralAmpModelerCore checkout; fetched from GitHub if it is absent")

if(CMAKE_VERSION VERSION_LESS 3.24)
  message(FATAL_ERROR "VALIS_WITH_NAM needs CMake 3.24 for whole-archive linking")
endif()

if(NOT EXISTS "${VALIS_NAM_DIR}/NAM/dsp.h")
  include(FetchContent)
  FetchContent_Declare(nam_core
    GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
    GIT_TAG v0.6.0
    GIT_SHALLOW TRUE)
  # Populate only: its own CMakeLists is a project of tools, not a library.
  FetchContent_GetProperties(nam_core)
  if(NOT nam_core_POPULATED)
    FetchContent_Populate(nam_core)
  endif()
  set(VALIS_NAM_DIR "${nam_core_SOURCE_DIR}")
endif()

find_package(Eigen3 3.4 QUIET NO_MODULE)
if(NOT TARGET Eigen3::Eigen)
  include(FetchContent)
  FetchContent_Declare(eigen
    GIT_REPOSITORY https://gitlab.com/libeigen/eigen.git
    GIT_TAG 3.4.0
    GIT_SHALLOW TRUE)
  set(EIGEN_BUILD_DOC OFF CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(eigen)
endif()

file(GLOB _nam_top "${VALIS_NAM_DIR}/NAM/*.cpp")
file(GLOB _nam_sub "${VALIS_NAM_DIR}/NAM/*/*.cpp")

add_library(valis_nam STATIC ${_nam_top} ${_nam_sub})
set_target_properties(valis_nam PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(valis_nam PUBLIC cxx_std_20)

# SYSTEM keeps NAM's own warnings out of Valis's warning flags.
target_include_directories(valis_nam SYSTEM PUBLIC
  ${VALIS_NAM_DIR}
  ${VALIS_NAM_DIR}/NAM
  ${VALIS_NAM_DIR}/Dependencies/nlohmann)
target_link_libraries(valis_nam PUBLIC Eigen3::Eigen)

# Single-precision samples, so the engine's float buffers need no widening.
target_compile_definitions(valis_nam PUBLIC NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
