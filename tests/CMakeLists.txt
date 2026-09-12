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
  ops/OpDispatcherTest.cpp
  console/ConsoleTest.cpp)

# The tokeniser lives in the plugin's UI layer, so its test needs the GUI
# module. It constructs no Component and opens no window, so it still runs
# headless.
add_executable(ui_TurtleCodeTokeniserTest ui/TurtleCodeTokeniserTest.cpp
                                          ${CMAKE_SOURCE_DIR}/src/ui/TurtleCodeTokeniser.cpp)
target_link_libraries(ui_TurtleCodeTokeniserTest PRIVATE valis_core juce::juce_gui_extra)
target_include_directories(ui_TurtleCodeTokeniserTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ui_TurtleCodeTokeniserTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
add_test(NAME ui_TurtleCodeTokeniserTest COMMAND ui_TurtleCodeTokeniserTest)

# The equipment schemes are header-only pure data: no GUI needed.
add_executable(ui_UiThemeTest ui/UiThemeTest.cpp)
target_link_libraries(ui_UiThemeTest PRIVATE valis_core)
add_test(NAME ui_UiThemeTest COMMAND ui_UiThemeTest)

foreach(_src ${VALIS_TESTS})
  string(REPLACE "/" "_" _name ${_src})
  string(REPLACE ".cpp" "" _name ${_name})
  add_executable(${_name} ${_src})
  target_link_libraries(${_name} PRIVATE valis_core)
  target_include_directories(${_name} PRIVATE ${CMAKE_SOURCE_DIR}/src)
  add_test(NAME ${_name} COMMAND ${_name})
endforeach()

# The chat client lives in the plugin's AI layer and uses juce_core JSON and
# ChildProcess. It makes no network calls here: the transport is stubbed.
add_executable(ai_ChatClientTest ai/ChatClientTest.cpp
                                 ${CMAKE_SOURCE_DIR}/src/ai/ChatClient.cpp)
target_link_libraries(ai_ChatClientTest PRIVATE valis_core juce::juce_core)
target_include_directories(ai_ChatClientTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ai_ChatClientTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
add_test(NAME ai_ChatClientTest COMMAND ai_ChatClientTest)

# The provider presets are pure data with no JUCE dependency.
add_executable(ai_AiProvidersTest ai/AiProvidersTest.cpp
                                  ${CMAKE_SOURCE_DIR}/src/ai/AiProviders.cpp)
target_link_libraries(ai_AiProvidersTest PRIVATE valis_core)
target_include_directories(ai_AiProvidersTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ai_AiProvidersTest COMMAND ai_AiProvidersTest)

# Rate-limit arithmetic is part of valis_core; the budget store that holds what
# each provider reported is not, so it is compiled in here.
add_executable(ai_RateLimitsTest ai/RateLimitsTest.cpp
                                 ${CMAKE_SOURCE_DIR}/src/ai/ProviderBudgets.cpp)
target_link_libraries(ai_RateLimitsTest PRIVATE valis_core)
target_include_directories(ai_RateLimitsTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
add_test(NAME ai_RateLimitsTest COMMAND ai_RateLimitsTest)

# The router walks the provider order. Every endpoint is stubbed, so no test
# here reaches the network or waits for a real rate-limit window.
add_executable(ai_AiRouterTest ai/AiRouterTest.cpp
                               ${CMAKE_SOURCE_DIR}/src/ai/AiRouter.cpp
                               ${CMAKE_SOURCE_DIR}/src/ai/ChatClient.cpp
                               ${CMAKE_SOURCE_DIR}/src/ai/AiProviders.cpp
                               ${CMAKE_SOURCE_DIR}/src/ai/ProviderBudgets.cpp)
target_link_libraries(ai_AiRouterTest PRIVATE valis_core juce::juce_core)
target_include_directories(ai_AiRouterTest PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_compile_definitions(ai_AiRouterTest PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
add_test(NAME ai_AiRouterTest COMMAND ai_AiRouterTest)
