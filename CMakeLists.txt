cmake_minimum_required(VERSION 3.22)
project(DarkMPE VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "")
if(NOT CMAKE_OSX_ARCHITECTURES)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "" FORCE)
endif()

option(DARKMPE_INSTALL "Copy plugins to ~/Library/Audio/Plug-Ins after build" OFF)

add_subdirectory(external/JUCE)

set(ENGINE_SOURCES
    Source/model/Phrase.cpp
    Source/engine/MelodyGenerator.cpp
    Source/engine/VoicingEngine.cpp
    Source/engine/CinematicEngine.cpp
    Source/engine/HarmonyEngine.cpp
    Source/engine/Humanize.cpp
    Source/engine/ExpressionShaper.cpp
    Source/engine/MpeRenderer.cpp
    Source/engine/MidiFileIO.cpp)

# ---------------------------------------------------------------- plugin
juce_add_plugin(DarkMPE
    COMPANY_NAME "Laherte"
    BUNDLE_ID "com.laherte.darkmpe"
    PLUGIN_MANUFACTURER_CODE Lahr
    PLUGIN_CODE DkMP
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "DarkMPE"
    IS_SYNTH TRUE
    NEEDS_MIDI_INPUT TRUE
    NEEDS_MIDI_OUTPUT TRUE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    VST3_CATEGORIES Instrument Generator
    AU_MAIN_TYPE kAudioUnitType_MusicDevice
    COPY_PLUGIN_AFTER_BUILD ${DARKMPE_INSTALL})

target_sources(DarkMPE PRIVATE
    ${ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PortHub.cpp
    Source/PluginEditor.cpp
    Source/ui/PianoRoll.cpp
    Source/ui/MpeMonitor.cpp)

target_include_directories(DarkMPE PRIVATE Source)

target_compile_definitions(DarkMPE PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0)

target_link_libraries(DarkMPE
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_processors
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)

# ---------------------------------------------------------------- tests
juce_add_console_app(DarkMPETests PRODUCT_NAME "DarkMPETests")
target_sources(DarkMPETests PRIVATE ${ENGINE_SOURCES} Tests/EngineTests.cpp)
target_include_directories(DarkMPETests PRIVATE Source)
target_compile_definitions(DarkMPETests PRIVATE JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0 JUCE_UNIT_TESTS=0)
target_link_libraries(DarkMPETests
    PRIVATE juce::juce_audio_basics
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)

enable_testing()
add_test(NAME EngineTests COMMAND DarkMPETests)

# ---------------------------------------------------------------- processor tests (real plugin processor, offline)
juce_add_console_app(DarkMPEProcessorTests PRODUCT_NAME "DarkMPEProcessorTests")
target_sources(DarkMPEProcessorTests PRIVATE
    ${ENGINE_SOURCES}
    Source/PluginProcessor.cpp
    Source/PortHub.cpp
    Source/PluginEditor.cpp
    Source/ui/PianoRoll.cpp
    Source/ui/MpeMonitor.cpp
    Tests/ProcessorTests.cpp)
target_include_directories(DarkMPEProcessorTests PRIVATE Source)
target_compile_definitions(DarkMPEProcessorTests PRIVATE
    JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0
    DARKMPE_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(DarkMPEProcessorTests
    PRIVATE juce::juce_audio_utils juce::juce_audio_processors
    PUBLIC juce::juce_recommended_config_flags juce::juce_recommended_warning_flags)
add_test(NAME ProcessorTests COMMAND DarkMPEProcessorTests)

# ---------------------------------------------------------------- host load test (loads the built VST3 like a DAW)
juce_add_console_app(HostLoadTest PRODUCT_NAME "HostLoadTest")
target_sources(HostLoadTest PRIVATE Tests/HostLoadTest.cpp)
target_compile_definitions(HostLoadTest PRIVATE JUCE_PLUGINHOST_VST3=1 JUCE_PLUGINHOST_AU=0 JUCE_USE_CURL=0 JUCE_WEB_BROWSER=0)
target_link_libraries(HostLoadTest PRIVATE juce::juce_audio_processors juce::juce_audio_utils
    PUBLIC juce::juce_recommended_config_flags)
