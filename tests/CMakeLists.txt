# tests/CMakeLists.txt
#
# One executable per test file, bare main() + <cassert>. Add a file here and to
# the list below; no framework, no registration boilerplate.

set(VALIS_TESTS
  rdf/VocabularyTest.cpp)

foreach(_src ${VALIS_TESTS})
  string(REPLACE "/" "_" _name ${_src})
  string(REPLACE ".cpp" "" _name ${_name})
  add_executable(${_name} ${_src})
  target_link_libraries(${_name} PRIVATE valis_core)
  target_include_directories(${_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${_name} COMMAND ${_name})
endforeach()
