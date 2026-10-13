cmake_minimum_required(VERSION 3.22)

project(NgDaw VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ------------------------------------------------------------------------------
# Tracktion Engine and the JUCE commit it pins (Tracktion's own submodule URL
# uses SSH, so JUCE is fetched separately over HTTPS).
# To build against local checkouts instead of downloading, configure with
#   -DFETCHCONTENT_SOURCE_DIR_TRACKTION=/path/to/tracktion_engine
#   -DFETCHCONTENT_SOURCE_DIR_JUCE=/path/to/JUCE
# ------------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(tracktion
    GIT_REPOSITORY https://github.com/Tracktion/tracktion_engine.git
    GIT_TAG        v3.2.0
    GIT_SHALLOW    TRUE
    GIT_SUBMODULES "")
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        19edd538429c93d277bf95b55aaa7e3eb545f951) # Tracktion v3.2.0's JUCE
FetchContent_GetProperties(tracktion)
if (NOT tracktion_POPULATED)
    FetchContent_Populate(tracktion)
endif()
FetchContent_GetProperties(juce)
if (NOT juce_POPULATED)
    FetchContent_Populate(juce)
endif()

add_subdirectory(${juce_SOURCE_DIR} ${CMAKE_BINARY_DIR}/juce EXCLUDE_FROM_ALL)
add_subdirectory(${tracktion_SOURCE_DIR}/modules ${CMAKE_BINARY_DIR}/tracktion EXCLUDE_FROM_ALL)

# ------------------------------------------------------------------------------
# ng_core: JUCE + Tracktion compiled once, plus the project model and the
# command layer. The app, the tests and (later) the MCP server all link this.
# ------------------------------------------------------------------------------
add_library(ng_core STATIC
    src/core/Session.cpp
    src/core/CommandRegistry.cpp
    src/core/CommandDispatcher.cpp
    src/core/ProjectCommands.cpp
    src/core/PluginCommands.cpp
    src/core/EffectCommands.cpp
    src/core/BusCommands.cpp
    src/core/OutputCommands.cpp
    src/core/SamplerCommands.cpp)

target_include_directories(ng_core PUBLIC src)

target_compile_definitions(ng_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    JUCE_USE_CURL=0
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1)

if (APPLE)
    target_compile_definitions(ng_core PUBLIC JUCE_PLUGINHOST_AU=1)
endif()

target_link_libraries(ng_core
    PRIVATE
        tracktion::tracktion_core
        tracktion::tracktion_engine
        tracktion::tracktion_graph
        juce::juce_audio_devices
        juce::juce_audio_processors
        juce::juce_audio_utils
        juce::juce_gui_extra
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags)

# JUCE modules are INTERFACE targets; linking them PRIVATE compiles their sources
# into ng_core only once. Re-export their include paths and definitions so the
# targets that link ng_core can use the headers.
target_include_directories(ng_core INTERFACE
    $<TARGET_PROPERTY:ng_core,INCLUDE_DIRECTORIES>)
target_compile_definitions(ng_core INTERFACE
    $<TARGET_PROPERTY:ng_core,COMPILE_DEFINITIONS>)

if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_link_libraries(ng_core PUBLIC atomic)
endif()

if (CMAKE_CXX_COMPILER_ID MATCHES "GNU")
    target_compile_options(ng_core PRIVATE -Wno-unknown-pragmas)
endif()

# ------------------------------------------------------------------------------
# The desktop app
# ------------------------------------------------------------------------------
juce_add_gui_app(NgDaw
    PRODUCT_NAME "Ng DAW"
    BUNDLE_ID "audio.ng.daw"
    COMPANY_NAME "Ng Audio"
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio recording")

target_sources(NgDaw PRIVATE
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/MixerView.cpp
    src/app/TimelineView.cpp
    src/app/PianoRoll.cpp
    src/app/PluginWindow.cpp
    src/app/SamplerEditor.cpp)

target_link_libraries(NgDaw PRIVATE ng_core juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Headless tests for the command layer and audio rendering
# ------------------------------------------------------------------------------
include(CTest)
if (BUILD_TESTING)
    # A tiny VST3 instrument, so scanning and loading are tested against a real plugin.
    juce_add_plugin(NgTestSynth
        PRODUCT_NAME "Ng Test Synth"
        COMPANY_NAME "Ng Audio"
        BUNDLE_ID "audio.ng.testsynth"
        PLUGIN_MANUFACTURER_CODE NgAu
        PLUGIN_CODE NgTs
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE)
    target_sources(NgTestSynth PRIVATE tests/TestSynth/TestSynth.cpp)
    target_compile_definitions(NgTestSynth PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                                  JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(NgTestSynth PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

    # And a tiny VST3 effect, for effects on mixer strips.
    juce_add_plugin(NgTestGain
        PRODUCT_NAME "Ng Test Gain"
        COMPANY_NAME "Ng Audio"
        BUNDLE_ID "audio.ng.testgain"
        PLUGIN_MANUFACTURER_CODE NgAu
        PLUGIN_CODE NgTg
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE)
    target_sources(NgTestGain PRIVATE tests/TestGain/TestGain.cpp)
    target_compile_definitions(NgTestGain PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                                 JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(NgTestGain PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

    # And a tiny two-output VST3 instrument, for multi-output instruments.
    juce_add_plugin(NgTestDrums
        PRODUCT_NAME "Ng Test Drums"
        COMPANY_NAME "Ng Audio"
        BUNDLE_ID "audio.ng.testdrums"
        PLUGIN_MANUFACTURER_CODE NgAu
        PLUGIN_CODE NgTd
        IS_SYNTH TRUE
        NEEDS_MIDI_INPUT TRUE
        FORMATS VST3
        VST3_AUTO_MANIFEST FALSE)
    target_sources(NgTestDrums PRIVATE tests/TestDrums/TestDrums.cpp)
    target_compile_definitions(NgTestDrums PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0
                                                  JUCE_DISPLAY_SPLASH_SCREEN=0)
    target_link_libraries(NgTestDrums PRIVATE juce::juce_audio_utils juce::juce_recommended_config_flags)

    juce_add_console_app(NgDawTests PRODUCT_NAME "NgDawTests")
    target_sources(NgDawTests PRIVATE tests/CommandTests.cpp)
    target_link_libraries(NgDawTests PRIVATE ng_core)
    add_dependencies(NgDawTests NgTestSynth_VST3 NgTestGain_VST3 NgTestDrums_VST3)

    # The folders holding the built test plugins, for the tests to scan.
    target_compile_definitions(NgDawTests PRIVATE
        NG_TEST_PLUGIN_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/NgTestSynth_artefacts/$<CONFIG>/VST3"
        NG_TEST_EFFECT_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/NgTestGain_artefacts/$<CONFIG>/VST3"
        NG_TEST_DRUMS_FOLDER="${CMAKE_CURRENT_BINARY_DIR}/NgTestDrums_artefacts/$<CONFIG>/VST3")
    add_test(NAME command_layer COMMAND NgDawTests)
endif()
