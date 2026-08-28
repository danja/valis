# CMakeLists.txt
cmake_minimum_required(VERSION 3.22)

# JUCE fatal-errors unless C is among the project languages.
project(valis VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

option(VALIS_BUILD_TESTS  "Build unit tests"                 ON)
option(VALIS_BUILD_PLUGIN "Build the JUCE plugin targets"    ON)
option(VALIS_WITH_MCP     "Build the in-process MCP server"  ON)
option(VALIS_WITH_CLAP    "Also build the CLAP format"       OFF)
set(VALIS_JUCE_DIR "/home/danny/github/JUCE" CACHE PATH "Path to the JUCE checkout")

include(FindOrFetchSerd)

if(NOT TARGET juce::juce_core)
  add_subdirectory(${VALIS_JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE EXCLUDE_FROM_ALL)
endif()

# --------------------------------------------------------------------------
# valis_core - RDF, model, compiler, DSP, engine, ops.
# Links juce_dsp but no GUI module, so every test is a plain console binary.
# --------------------------------------------------------------------------
add_library(valis_core STATIC
  src/rdf/Vocabulary.cpp
  src/rdf/TurtleStore.cpp
  src/model/Ontology.cpp
  src/model/CircuitModel.cpp
  src/compiler/CircuitCompiler.cpp
  src/dsp/ElementRegistry.cpp
  src/dsp/elements/Sources.cpp
  src/dsp/elements/Filters.cpp
  src/dsp/elements/Transfers.cpp
  src/dsp/elements/Dynamics.cpp
  src/dsp/elements/Utility.cpp)

target_include_directories(valis_core
  PUBLIC  ${CMAKE_CURRENT_SOURCE_DIR}/include
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_link_libraries(valis_core
  PUBLIC
    valis::sord
    juce::juce_dsp
  PRIVATE
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

target_compile_definitions(valis_core PUBLIC
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  # JUCE bundles its own serd/sord/lilv to *host* LV2 plugins. Enabling that
  # would link two copies of serd into one binary. Valis hosts nothing, so keep
  # it off; if it is ever needed, drop our serd and use JUCE's.
  JUCE_PLUGINHOST_LV2=0
  VALIS_VOCABS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/vocabs"
  VALIS_EXAMPLES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/examples")

# --------------------------------------------------------------------------
# The plugin
# --------------------------------------------------------------------------
if(VALIS_BUILD_PLUGIN)
  set(VALIS_FORMATS Standalone VST3 LV2)

  juce_add_plugin(valis_plugin
    PRODUCT_NAME                "Valis"
    PLUGIN_NAME                 "Valis"
    COMPANY_NAME                "Danny Ayers"
    COMPANY_WEBSITE             "https://github.com/danja/valis"
    BUNDLE_ID                   "org.purl.stuff.valis"
    # Both codes must be set explicitly: JUCE randomises PLUGIN_CODE on every
    # configure otherwise, and hosts then lose track of the plugin.
    PLUGIN_MANUFACTURER_CODE    Dnja
    PLUGIN_CODE                 Vals
    # Must match https?://.* or urn:.* or configure fatal-errors. Never change it.
    LV2URI                      "urn:valis:valis"
    LV2_PLUGIN_CLASS            lv2:Plugin
    VST3_CATEGORIES             Fx Filter Distortion
    IS_SYNTH                    FALSE
    NEEDS_MIDI_INPUT            TRUE
    NEEDS_MIDI_OUTPUT           FALSE
    IS_MIDI_EFFECT              FALSE
    # Without this, hosts swallow every keystroke aimed at the Turtle editor.
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD     FALSE
    NEEDS_WEB_BROWSER           FALSE
    NEEDS_CURL                  FALSE
    FORMATS                     ${VALIS_FORMATS})

  target_sources(valis_plugin PRIVATE
    src/plugin/ValisProcessor.cpp
    src/ui/ValisEditor.cpp)

  target_include_directories(valis_plugin PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

  target_compile_definitions(valis_plugin PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

  target_link_libraries(valis_plugin
    PRIVATE
      valis_core
      juce::juce_audio_utils
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)

  if(VALIS_WITH_CLAP)
    include(FindOrFetchClap)
    clap_juce_extensions_plugin(TARGET valis_plugin
      CLAP_ID "org.purl.stuff.valis"
      CLAP_FEATURES audio-effect filter distortion)
  endif()
endif()

# --------------------------------------------------------------------------
# Tests - one bare-main() executable per file, registered with CTest.
# --------------------------------------------------------------------------
if(VALIS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
