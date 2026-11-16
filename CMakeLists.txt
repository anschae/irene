cmake_minimum_required(VERSION 3.22)

# ==============================================================================
# Plugin identity -- everything that names the plugin lives in this block.
# NOTE: the manufacturer/plugin codes and the bundle id identify the plugin to
# DAWs. Changing them after release makes existing projects lose the plugin.
# ==============================================================================
set(IRENE_PLUGIN_NAME        "IRene")
set(IRENE_VERSION            "0.1.0")
set(IRENE_COMPANY_NAME       "anschae")
set(IRENE_MANUFACTURER_CODE  "Ansc")   # 4 chars, at least one uppercase (AU)
set(IRENE_PLUGIN_CODE        "IRne")   # 4 chars, at least one uppercase (AU)
set(IRENE_BUNDLE_ID          "com.anschae.irene")
set(IRENE_JUCE_TAG           "9.0.5")  # pinned JUCE release

# ==============================================================================
# Platform settings (must be set before project())
# ==============================================================================
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures (universal)")
# Static MSVC runtime: no VC++ redistributable needed on the user's machine.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>" CACHE STRING "MSVC runtime")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

project(IRene VERSION ${IRENE_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_XCODE_GENERATE_SCHEME OFF)

option(IRENE_BUILD_TESTS "Build the unit test runner" ON)
option(IRENE_BUILD_PLUGIN "Build the plugin targets (VST3/AU/Standalone)" ON)
option(IRENE_BUILD_TOOLS "Build developer tools (UI snapshot renderer)" OFF)

# ==============================================================================
# JUCE
# ==============================================================================
include(FetchContent)
FetchContent_Declare(juce
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${IRENE_JUCE_TAG}
    GIT_SHALLOW    TRUE
    GIT_PROGRESS   FALSE)
FetchContent_MakeAvailable(juce)

# ==============================================================================
# Shared code: compiled into both the plugin and the unit tests.
# (An INTERFACE library, because JUCE modules must be compiled once per final
# target; a static library would duplicate JUCE symbols.)
# ==============================================================================
add_library(IReneShared INTERFACE)

target_sources(IReneShared INTERFACE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/Config.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginProcessor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/PluginEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/GlobalSettings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/GlobalSettings.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/PresetManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/app/PresetManager.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PresetBar.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/PresetBar.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/IRLibrary.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/library/IRLibrary.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/BrowserPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/BrowserPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Theme.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Theme.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Widgets.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/Widgets.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SlotStrip.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SlotStrip.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FilterEditor.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/FilterEditor.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/GlobalSection.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/GlobalSection.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsPanel.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ui/SettingsPanel.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/params/Settings.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/params/Parameters.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/params/Parameters.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Svf.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/FilterBlock.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/FilterBlock.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/FractionalDelay.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/SlotDsp.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/SlotDsp.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Engine.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/dsp/Engine.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRSource.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRSource.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRProcessing.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRProcessing.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRSerialisation.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/IRSerialisation.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/SlotManager.h
    ${CMAKE_CURRENT_SOURCE_DIR}/src/ir/SlotManager.cpp)

target_include_directories(IReneShared INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/src)

target_compile_definitions(IReneShared INTERFACE
    IRENE_PLUGIN_NAME="${IRENE_PLUGIN_NAME}"
    IRENE_COMPANY_NAME="${IRENE_COMPANY_NAME}"
    IRENE_VERSION_STRING="${IRENE_VERSION}"
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_REPORT_APP_USAGE=0)

target_link_libraries(IReneShared INTERFACE
    juce::juce_audio_utils
    juce::juce_dsp
    juce::juce_recommended_config_flags
    juce::juce_recommended_lto_flags)

if(MSVC)
    target_compile_options(IReneShared INTERFACE /W4)
else()
    target_compile_options(IReneShared INTERFACE -Wall -Wextra)
endif()

# ==============================================================================
# Plugin
# ==============================================================================
if(IRENE_BUILD_PLUGIN)
    set(IRENE_FORMATS VST3 Standalone)
    if(APPLE)
        list(APPEND IRENE_FORMATS AU)
    endif()

    juce_add_plugin(IRene
        PRODUCT_NAME                "${IRENE_PLUGIN_NAME}"
        COMPANY_NAME                "${IRENE_COMPANY_NAME}"
        BUNDLE_ID                   "${IRENE_BUNDLE_ID}"
        PLUGIN_MANUFACTURER_CODE    ${IRENE_MANUFACTURER_CODE}
        PLUGIN_CODE                 ${IRENE_PLUGIN_CODE}
        FORMATS                     ${IRENE_FORMATS}
        IS_SYNTH                    FALSE
        NEEDS_MIDI_INPUT            FALSE
        NEEDS_MIDI_OUTPUT           FALSE
        IS_MIDI_EFFECT              FALSE
        EDITOR_WANTS_KEYBOARD_FOCUS TRUE
        AU_MAIN_TYPE                kAudioUnitType_Effect
        VST3_CATEGORIES             Fx Tools
        COPY_PLUGIN_AFTER_BUILD     FALSE)

    target_sources(IRene PRIVATE src/PluginEntry.cpp)
    target_link_libraries(IRene PRIVATE IReneShared)
    # Plugins must never run modal loops: they would block the host.
    target_compile_definitions(IRene PUBLIC JUCE_MODAL_LOOPS_PERMITTED=0)
endif()

# ==============================================================================
# Unit tests
# ==============================================================================
if(IRENE_BUILD_TESTS)
    enable_testing()

    juce_add_console_app(IReneTests PRODUCT_NAME "IReneTests")

    target_sources(IReneTests PRIVATE
        tests/TestMain.cpp
        tests/TestHelpers.h
        tests/FilterTests.cpp
        tests/IRProcessingTests.cpp
        tests/EngineTests.cpp
        tests/ProcessorTests.cpp
        tests/SettingsAndUiTests.cpp
        tests/LibraryTests.cpp
        tests/PresetTests.cpp)

    target_link_libraries(IReneTests PRIVATE IReneShared)
    # The tests pump the message loop by hand, which needs this switch.
    target_compile_definitions(IReneTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

    add_test(NAME IReneTests COMMAND IReneTests)
endif()

# ==============================================================================
# Developer tools (not built by default)
# ==============================================================================
if(IRENE_BUILD_TOOLS)
    # Renders the editor to a PNG without a DAW: handy for checking layouts.
    juce_add_console_app(IReneSnapshot PRODUCT_NAME "IReneSnapshot")
    target_sources(IReneSnapshot PRIVATE tools/Snapshot.cpp)
    target_link_libraries(IReneSnapshot PRIVATE IReneShared)
    target_compile_definitions(IReneSnapshot PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
endif()
