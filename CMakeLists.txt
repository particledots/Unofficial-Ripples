cmake_minimum_required(VERSION 3.22)
project(UnofficialRipplesPlugin VERSION 0.5.0)   # raise this with every build that changes the plug-in's inputs/outputs, so Logic re-reads it

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
  # The GitHub workflow passes -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" for a universal build.
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
endif()

include(FetchContent)
FetchContent_Declare(JUCE
  GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 8.0.6
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

set(FORMATS VST3 Standalone)
if(APPLE)
  list(APPEND FORMATS AU)
endif()

juce_add_plugin(UnofficialRipples
  COMPANY_NAME "particledots"
  PLUGIN_MANUFACTURER_CODE Ptdt
  PLUGIN_CODE URpa
  FORMATS ${FORMATS}
  PRODUCT_NAME "Unofficial Ripples"
  IS_SYNTH FALSE
  NEEDS_MIDI_INPUT FALSE
  NEEDS_MIDI_OUTPUT FALSE
  AU_MAIN_TYPE kAudioUnitType_Effect
  COPY_PLUGIN_AFTER_BUILD TRUE)

target_sources(UnofficialRipples PRIVATE src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(UnofficialRipples PRIVATE src)
target_compile_definitions(UnofficialRipples PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(UnofficialRipples PRIVATE
  juce::juce_audio_utils
  juce::juce_recommended_config_flags
  juce::juce_recommended_warning_flags)

# --- Test of the filter model (no JUCE):  cmake --build build --target ripples_test && build/ripples_test ---
add_executable(ripples_test EXCLUDE_FROM_ALL tests/ripples_test.cpp)
target_include_directories(ripples_test PRIVATE src)

# --- Picture of the panel (used by the build):  cmake --build build --target ripples_snapshot ---
juce_add_console_app(ripples_snapshot PRODUCT_NAME "ripples_snapshot")
set_target_properties(ripples_snapshot PROPERTIES EXCLUDE_FROM_ALL TRUE)
target_sources(ripples_snapshot PRIVATE tools/snapshot.cpp src/PluginProcessor.cpp src/PluginEditor.cpp)
target_include_directories(ripples_snapshot PRIVATE src)
target_compile_definitions(ripples_snapshot PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_MODAL_LOOPS_PERMITTED=0)
target_link_libraries(ripples_snapshot PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
