# tests/CMakeLists.txt
#
# One executable per test file, bare main() + <cassert>. Add a file here and to
# the list below; no framework, no registration boilerplate.

set(VALIS_TESTS
  rdf/VocabularyTest.cpp
  rdf/TurtleStoreTest.cpp
  model/OntologyTest.cpp
  compiler/CircuitCompilerTest.cpp
  dsp/ElementRegistryTest.cpp
  dsp/ElementBehaviourTest.cpp
  engine/ValisEngineTest.cpp
  ops/OpDispatcherTest.cpp)

# The tokeniser lives in the plugin's UI layer, so its test needs the GUI
# module. It constructs no Component and opens no window, so it still runs
# headless.
add_executable(ui_TurtleCodeTokeniserTest ui/TurtleCodeTokeniserTest.cpp
                                          ${CMAKE_SOURCE_DIR}/src/ui/TurtleCodeTokeniser.cpp)
target_link_libraries(ui_TurtleCodeTokeniserTest PRIVATE valis_core juce::juce_gui_extra)
target_include_directories(ui_TurtleCodeTokeniserTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ui_TurtleCodeTokeniserTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
add_test(NAME ui_TurtleCodeTokeniserTest COMMAND ui_TurtleCodeTokeniserTest)

foreach(_src ${VALIS_TESTS})
  string(REPLACE "/" "_" _name ${_src})
  string(REPLACE ".cpp" "" _name ${_name})
  add_executable(${_name} ${_src})
  target_link_libraries(${_name} PRIVATE valis_core)
  target_include_directories(${_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${_name} COMMAND ${_name})
endforeach()
