cmake_minimum_required(VERSION 3.22)

project(IRComposer VERSION 0.2.4)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(APPLE)
    set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Build architectures for macOS" FORCE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS target" FORCE)
endif()

include(FetchContent)
FetchContent_Declare(
    JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG 8.0.4
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(JUCE)

juce_add_plugin(IRComposer
    COMPANY_NAME "Moth Production"
    BUNDLE_ID "com.mothproduction.ircomposer"
    PLUGIN_MANUFACTURER_CODE MthP
    PLUGIN_CODE IRCp
    FORMATS VST3 AU Standalone
    PRODUCT_NAME "IR Composer"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Restoration
    AU_MAIN_TYPE kAudioUnitType_Effect
)

# Lay artefacts out as build/<FORMAT>/... instead of the JUCE default of
# build/IRComposer_artefacts/<CONFIG>/<FORMAT>/..., so each format's build result
# lands directly under a top-level Standalone/VST3/AU folder.
set_target_properties(IRComposer PROPERTIES
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
)

set(IRCOMPOSER_SOURCES
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/IRSlot.cpp
    Source/BlendEngine.cpp
    Source/IRFileLoader.cpp
    Source/ExportEngine.cpp
    Source/SlotWaveformComponent.cpp
    Source/CombinedWaveformComponent.cpp
    Source/MasterEQEditor.cpp
    Source/MasterSpectrumAnalyzer.cpp
    Source/SlotControlPanel.cpp
    Source/BlendPadComponent.cpp
    Source/ExportPanel.cpp
    Source/IRSlotsPage.cpp
    Source/MasterEQPage.cpp
)

target_sources(IRComposer PRIVATE ${IRCOMPOSER_SOURCES})

target_compile_definitions(IRComposer PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
)

target_link_libraries(IRComposer
    PRIVATE juce::juce_audio_utils juce::juce_audio_formats juce::juce_dsp
    PUBLIC juce::juce_recommended_config_flags
           juce::juce_recommended_lto_flags
           juce::juce_recommended_warning_flags
)

option(IRCOMPOSER_BUILD_TESTS "Build DSP unit tests" ON)
if(IRCOMPOSER_BUILD_TESTS)
    enable_testing()
    juce_add_console_app(IRComposerTests PRODUCT_NAME "IR Composer Tests")
    target_sources(IRComposerTests PRIVATE
        Tests/DspTests.cpp
        Source/IRSlot.cpp)
    target_link_libraries(IRComposerTests PRIVATE juce::juce_dsp juce::juce_audio_basics
                          PUBLIC juce::juce_recommended_config_flags)
    add_test(NAME IRComposerDSP COMMAND IRComposerTests)

    add_executable(IRComposerIntegrationTests Tests/IntegrationTests.cpp Tests/ChainRender.cpp)
    target_compile_definitions(IRComposerIntegrationTests PRIVATE
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(IRComposerIntegrationTests PRIVATE
        IRComposer juce::juce_audio_utils juce::juce_dsp)
    add_test(NAME IRComposerIntegration COMMAND IRComposerIntegrationTests)
    set_tests_properties(IRComposerIntegration PROPERTIES TIMEOUT 30)
endif()
