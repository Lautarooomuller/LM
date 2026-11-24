cmake_minimum_required(VERSION 3.22)
project(LM VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(JUCE)

option(LM_COPY_AFTER_BUILD "Copiar el VST3 a la carpeta de plugins del sistema" ON)

juce_add_plugin(LM
    COMPANY_NAME "MiEstudio"
    PRODUCT_NAME "LM"
    PLUGIN_MANUFACTURER_CODE Lmst
    PLUGIN_CODE LM01
    FORMATS VST3 Standalone
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    COPY_PLUGIN_AFTER_BUILD ${LM_COPY_AFTER_BUILD})

target_sources(LM PRIVATE
    PluginProcessor.cpp
    PluginProcessor.h
    Dsp.h)

target_compile_definitions(LM PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0)

target_link_libraries(LM
    PRIVATE
        juce::juce_audio_utils
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
