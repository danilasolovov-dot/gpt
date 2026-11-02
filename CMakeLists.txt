cmake_minimum_required(VERSION 3.22)

project(VinylPilot VERSION 0.1.0 LANGUAGES CXX)

# JUCE is downloaded automatically on the first configure. A local checkout can
# be supplied with -DJUCE_DIR=/path/to/JUCE for fully offline builds.
if (DEFINED JUCE_DIR)
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG 8.0.8
        GIT_SHALLOW TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

juce_add_plugin(VinylPilot
    COMPANY_NAME "Solovov Audio"
    BUNDLE_ID "com.solovovaudio.vinylpilot"
    PLUGIN_MANUFACTURER_CODE Slvv
    PLUGIN_CODE VnP1
    FORMATS VST3
    PRODUCT_NAME "Vinyl Pilot"
    DESCRIPTION "Vinyl texture and live turntable scratching"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE)

juce_generate_juce_header(VinylPilot)

target_sources(VinylPilot PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/VinylEngine.h)

target_compile_features(VinylPilot PRIVATE cxx_std_17)
target_compile_definitions(VinylPilot PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(VinylPilot PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags)
