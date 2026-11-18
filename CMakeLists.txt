cmake_minimum_required(VERSION 3.22)

project(RTO119 VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(RTO119_BUILD_PLUGIN "Build the JUCE plugin (needs JUCE)" ON)
option(RTO119_BUILD_TESTS "Build the DSP unit tests (no JUCE needed)" ON)

# ---------------------------------------------------------------------------
# DSP unit tests: plain C++, no JUCE.
# ---------------------------------------------------------------------------
if(RTO119_BUILD_TESTS)
    enable_testing()
    add_executable(rto119_dsp_tests tests/dsp_tests.cpp)
    target_include_directories(rto119_dsp_tests PRIVATE src)
    add_test(NAME dsp_tests COMMAND rto119_dsp_tests)
endif()

if(NOT RTO119_BUILD_PLUGIN)
    return()
endif()

# ---------------------------------------------------------------------------
# JUCE. Use a local copy with -DJUCE_DIR=path, otherwise download the pinned tag.
# ---------------------------------------------------------------------------
set(JUCE_DIR "" CACHE PATH "Path to a local JUCE checkout (optional)")
if(JUCE_DIR)
    add_subdirectory(${JUCE_DIR} ${CMAKE_BINARY_DIR}/JUCE)
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 9.0.5
        GIT_SHALLOW ON)
    FetchContent_MakeAvailable(JUCE)
endif()

# VST2 is intentionally not built: Steinberg no longer licenses VST2 to new developers.
juce_add_plugin(RTO119
    COMPANY_NAME "RTO Beats"
    PRODUCT_NAME "RTO 119 Compander"
    PLUGIN_MANUFACTURER_CODE Rtob
    PLUGIN_CODE R119
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    VST3_CATEGORIES Fx Dynamics
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(RTO119)

target_sources(RTO119 PRIVATE
    src/PluginProcessor.cpp
    src/PluginEditor.cpp)

target_include_directories(RTO119 PRIVATE src)

target_compile_definitions(RTO119 PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(RTO119
    PRIVATE
        juce::juce_audio_utils
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------------------
# Processor tests (D1P-22): a console test that drives RTO119Processor directly.
# No editor, no GUI. Links the RTO119 target, which is the static shared-code library
# built by juce_add_plugin (output name RTO119_SharedCode).
# ---------------------------------------------------------------------------
if(RTO119_BUILD_TESTS)
    add_executable(rto119_processor_tests tests/processor_tests.cpp)
    target_include_directories(rto119_processor_tests PRIVATE
        src
        $<TARGET_PROPERTY:RTO119,JUCE_GENERATED_SOURCES_DIRECTORY>)   # JuceHeader.h
    target_link_libraries(rto119_processor_tests PRIVATE
        RTO119
        juce::juce_audio_plugin_client   # JuceHeader.h includes it; headers only
        juce::juce_audio_utils
        juce::juce_dsp)
    add_test(NAME processor_tests COMMAND rto119_processor_tests)
endif()
