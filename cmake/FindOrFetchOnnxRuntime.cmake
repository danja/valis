# cmake/FindOrFetchOnnxRuntime.cmake
#
# Provides the imported target valis::onnxruntime (ONNX Runtime, https://onnxruntime.ai).
# A system install is used if find_package finds one; otherwise Microsoft's prebuilt CPU release is
# fetched, pinned by version and checked against its SHA-256. Used only by src/shot/NeuralEars.cpp.

include(FetchContent)

find_package(onnxruntime CONFIG QUIET)
if(onnxruntime_FOUND)
  message(STATUS "valis: using system ONNX Runtime")
  add_library(valis_onnxruntime INTERFACE)
  target_link_libraries(valis_onnxruntime INTERFACE onnxruntime::onnxruntime)
else()
  message(STATUS "valis: fetching the ONNX Runtime 1.31.1 prebuilt release")
  FetchContent_Declare(onnxruntime_prebuilt
    URL      https://github.com/microsoft/onnxruntime/releases/download/v1.31.1/onnxruntime-linux-x64-1.31.1.tgz
    URL_HASH SHA256=c849e36a1ef3e22d4ff217b9d1ce4a3cf5b9467b975610913ae959ad5b16bdc9)
  FetchContent_MakeAvailable(onnxruntime_prebuilt)
  add_library(valis_onnxruntime SHARED IMPORTED GLOBAL)
  set_target_properties(valis_onnxruntime PROPERTIES
    IMPORTED_LOCATION             ${onnxruntime_prebuilt_SOURCE_DIR}/lib/libonnxruntime.so
    INTERFACE_INCLUDE_DIRECTORIES ${onnxruntime_prebuilt_SOURCE_DIR}/include)
  set(VALIS_ONNX_LIB_DIR ${onnxruntime_prebuilt_SOURCE_DIR}/lib CACHE INTERNAL "")
endif()
add_library(valis::onnxruntime ALIAS valis_onnxruntime)
