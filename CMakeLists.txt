cmake_minimum_required(VERSION 3.24)
project(SmartTime VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
option(SMART_TIME_BUILD_PLUGIN "Build JUCE VST3" ON)
option(SMART_TIME_VST3_MANIFEST "Generate VST3 metadata by executing the target helper" ON)
include(CTest)
include(FetchContent)
FetchContent_Declare(rubberband
  GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
  GIT_TAG 2be46b0dffb13273a67396c77bc9278736bb03d2)
FetchContent_MakeAvailable(rubberband)
add_library(SmartTimeStretch STATIC ${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp)
target_include_directories(SmartTimeStretch PUBLIC ${rubberband_SOURCE_DIR})
if(MSVC)
  target_compile_definitions(SmartTimeStretch PRIVATE _USE_MATH_DEFINES NOMINMAX)
endif()
add_library(SmartTimeCore STATIC src/core/Engine.cpp src/core/Wav.cpp)
target_include_directories(SmartTimeCore PUBLIC src/core)
target_link_libraries(SmartTimeCore PUBLIC SmartTimeStretch)
if(MSVC)
  target_compile_options(SmartTimeCore PRIVATE /W4 /EHsc)
else()
  target_compile_options(SmartTimeCore PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(BUILD_TESTING)
  add_executable(SmartTimeTests tests/CoreTests.cpp)
  target_link_libraries(SmartTimeTests PRIVATE SmartTimeCore)
  foreach(case IN ITEMS wav invalid silence attacks tempo map render cancel stereo)
    add_test(NAME core_${case} COMMAND SmartTimeTests ${case})
  endforeach()
endif()
if(SMART_TIME_BUILD_PLUGIN)
  find_package(JUCE 8.0.6 EXACT CONFIG QUIET)
  if(NOT JUCE_FOUND)
  FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 51a8a6d7aeae7326956d747737ccf1575e61e209)
  FetchContent_MakeAvailable(juce)
  endif()
  juce_add_plugin(SmartTime
    COMPANY_NAME CHi
    BUNDLE_ID audio.chi.smarttime
    PLUGIN_MANUFACTURER_CODE Chii
    PLUGIN_CODE Smti
    FORMATS VST3
    PRODUCT_NAME "SMART TIME"
    DESCRIPTION "Intelligent Time Correction"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_AUTO_MANIFEST ${SMART_TIME_VST3_MANIFEST}
    VST3_CATEGORIES Fx Tools)
  target_sources(SmartTime PRIVATE src/plugin/Plugin.cpp)
  target_link_libraries(SmartTime PRIVATE SmartTimeCore juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)
  target_compile_definitions(SmartTime PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)
  if(BUILD_TESTING)
    add_executable(SmartTimeLifecycle tests/PluginLifecycle.cpp)
    target_link_libraries(SmartTimeLifecycle PRIVATE SmartTime SmartTimeCore)
    # JUCE modules are already compiled into the shared-code static library.
    get_target_property(smart_time_juce_modules juce::juce_core INTERFACE_JUCE_MODULE_PATH)
    target_include_directories(SmartTimeLifecycle PRIVATE ${smart_time_juce_modules})
    target_compile_definitions(SmartTimeLifecycle PRIVATE JUCE_GLOBAL_MODULE_SETTINGS_INCLUDED=1)
    add_test(NAME plugin_lifecycle COMMAND SmartTimeLifecycle)
  endif()
endif()
