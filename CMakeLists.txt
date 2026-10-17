cmake_minimum_required(VERSION 3.22)

project(NgDaw VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Link the C++ runtime into the executables on Windows, so the installer runs on a clean PC
# without the Visual C++ redistributable.
if (CMAKE_SYSTEM_NAME STREQUAL "Linux")
    set(NG_LINUX ON)
else()
    set(NG_LINUX OFF)
endif()

set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

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

# ------------------------------------------------------------------------------
# Rubber Band does the time-stretching and pitch-shifting of audio clips (GPL v2 or later, or
# commercial: https://breakfastquay.com/rubberband/). Tracktion compiles it in as one file.
# Tracktion asks Rubber Band for its older R2 engine; for melodic material we switch that to
# R3, Rubber Band's highest quality engine. Clips are stretched into a cached file in the
# background, so the extra CPU it costs isn't paid while the song plays.
# ------------------------------------------------------------------------------
FetchContent_Declare(rubberband
    GIT_REPOSITORY https://github.com/breakfastquay/rubberband.git
    GIT_TAG        v4.0.0
    GIT_SHALLOW    TRUE)
FetchContent_GetProperties(rubberband)
if (NOT rubberband_POPULATED)
    FetchContent_Populate(rubberband)
endif()

# Tracktion includes <rubberband/single/RubberBandSingle.cpp>; this points that at the download.
set(NG_RUBBERBAND_INCLUDE ${CMAKE_BINARY_DIR}/rubberband_include)
file(WRITE ${NG_RUBBERBAND_INCLUDE}/rubberband/single/RubberBandSingle.cpp
     "#include \"${rubberband_SOURCE_DIR}/single/RubberBandSingle.cpp\"\n")

set(NG_TIMESTRETCH_CPP ${tracktion_SOURCE_DIR}/modules/tracktion_engine/timestretch/tracktion_TimeStretch.cpp)
file(READ ${NG_TIMESTRETCH_CPP} NG_TIMESTRETCH_SOURCE)
string(REPLACE "| RubberBand::RubberBandStretcher::OptionWindowShort;"
               "| RubberBand::RubberBandStretcher::OptionEngineFiner;"
               NG_TIMESTRETCH_PATCHED "${NG_TIMESTRETCH_SOURCE}")
if (NOT NG_TIMESTRETCH_PATCHED STREQUAL NG_TIMESTRETCH_SOURCE)
    file(WRITE ${NG_TIMESTRETCH_CPP} "${NG_TIMESTRETCH_PATCHED}")
elseif (NOT NG_TIMESTRETCH_SOURCE MATCHES "OptionEngineFiner")
    message(WARNING "Couldn't switch Tracktion's Rubber Band stretcher to the R3 engine; clips will use R2")
endif()

# R3 can ask for more input at once than the real-time reader's buffers hold (8192 frames, and
# a buffer that size holds one frame less), which overruns them. Ask for at most half that, so
# there's room for output still waiting too; Rubber Band takes the rest next time.
file(READ ${NG_TIMESTRETCH_CPP} NG_TIMESTRETCH_SOURCE)
string(REPLACE "return numSamplesToDrop > 0 ? std::min (numFramesRequired * 6, getMaxFramesNeeded())\n                                    : numFramesRequired;"
               "return std::min (numSamplesToDrop > 0 ? numFramesRequired * 6 : numFramesRequired, getMaxFramesNeeded() / 2);"
               NG_TIMESTRETCH_PATCHED "${NG_TIMESTRETCH_SOURCE}")
if (NOT NG_TIMESTRETCH_PATCHED STREQUAL NG_TIMESTRETCH_SOURCE)
    file(WRITE ${NG_TIMESTRETCH_CPP} "${NG_TIMESTRETCH_PATCHED}")
elseif (NOT NG_TIMESTRETCH_SOURCE MATCHES "getMaxFramesNeeded\\(\\) / 2\\)")
    message(WARNING "Couldn't cap Rubber Band's input request; real-time stretching may overrun")
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
    src/core/ClipCommands.cpp
    src/core/TransportCommands.cpp
    src/core/PluginCommands.cpp
    src/core/EffectCommands.cpp
    src/core/BusCommands.cpp
    src/core/OutputCommands.cpp
    src/core/SamplerCommands.cpp
    src/core/LibraryCommands.cpp
    src/core/AudioCommands.cpp
    src/core/ExportCommands.cpp
    src/core/AutomationCommands.cpp
    src/core/Bridge.cpp
    src/core/McpServer.cpp
    src/core/Autosave.cpp
    src/core/SampleIndex.cpp
    src/core/AiChat.cpp)

target_include_directories(ng_core PUBLIC src)
target_include_directories(ng_core PRIVATE ${NG_RUBBERBAND_INCLUDE})

target_compile_definitions(ng_core PUBLIC
    JUCE_PLUGINHOST_VST3=1
    # The chat panel talks to AI services over HTTPS: through libcurl on Linux, the OS elsewhere.
    JUCE_USE_CURL=$<BOOL:${NG_LINUX}>
    JUCE_WEB_BROWSER=0
    JUCE_MODAL_LOOPS_PERMITTED=1
    JUCE_STRICT_REFCOUNTEDPOINTER=1
    JUCE_STANDALONE_APPLICATION=1
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_USE_MP3AUDIOFORMAT=1
    TRACKTION_ENABLE_TIMESTRETCH_SOUNDTOUCH=1
    TRACKTION_ENABLE_TIMESTRETCH_RUBBERBAND=1
    TRACKTION_BUILD_RUBBERBAND=1
    NG_VERSION="${PROJECT_VERSION}")

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

if (NG_LINUX)
    find_package(CURL REQUIRED)
    target_link_libraries(ng_core PUBLIC atomic CURL::libcurl)
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
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Audio recording")

target_sources(NgDaw PRIVATE
    src/app/Main.cpp
    src/app/MainComponent.cpp
    src/app/MixerView.cpp
    src/app/TimelineView.cpp
    src/app/PianoRoll.cpp
    src/app/PluginWindow.cpp
    src/app/SamplerEditor.cpp
    src/app/SampleEditor.cpp
    src/app/BrowserPanel.cpp
    src/app/ExportPanel.cpp
    src/app/CommandLogPanel.cpp
    src/app/PreferencesPanel.cpp
    src/app/HistoryPanel.cpp
    src/app/ChatPanel.cpp
    src/app/Theme.cpp
    src/app/DockPanel.cpp
    src/app/ChannelRack.cpp)

target_link_libraries(NgDaw PRIVATE ng_core juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# The MCP server: lets Claude and other AI clients drive the running app
# ------------------------------------------------------------------------------
juce_add_console_app(NgDawMcp PRODUCT_NAME "NgDawMcp")
target_sources(NgDawMcp PRIVATE src/mcp/McpMain.cpp)
target_link_libraries(NgDawMcp PRIVATE ng_core juce::juce_recommended_warning_flags)

# ------------------------------------------------------------------------------
# Headless tests for the command layer and audio rendering
# ------------------------------------------------------------------------------
include(CTest)
if (BUILD_TESTING)
    # A tiny VST3 instrument, so scanning and loading are tested against a real plugin.
    juce_add_plugin(NgTestSynth
        PRODUCT_NAME "Ng Test Synth"
        COMPANY_NAME "Ng Audio"
    VERSION ${PROJECT_VERSION}
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
    VERSION ${PROJECT_VERSION}
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
    VERSION ${PROJECT_VERSION}
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
    # One process for every test. Run one by itself with `NgDawTests =testName` (or every test
    # with a word in its name, `NgDawTests Bridge`). Not one ctest test per function: tearing
    # down the engine after a VST3 plugin has run sometimes crashes in JUCE's Linux run loop,
    # and 45 teardowns instead of one turn that into a flaky CI.
    add_test(NAME command_layer COMMAND NgDawTests)
endif()
