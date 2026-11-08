cmake_minimum_required(VERSION 3.22)

project(WavethingsAutosampler VERSION 0.1.6 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")

option(WT_BUILD_TESTS "Build the console test runner" ON)
option(WT_BUILD_APP "Build the application" ON)

# ------------------------------------------------------------------------------
# JUCE: either a local checkout (-DJUCE_DIR=/path/to/JUCE) or fetched from GitHub
# ------------------------------------------------------------------------------
set(WT_JUCE_TAG "8.0.15" CACHE STRING "JUCE tag to fetch")

if(DEFINED JUCE_DIR AND EXISTS "${JUCE_DIR}/CMakeLists.txt")
    add_subdirectory("${JUCE_DIR}" "${CMAKE_BINARY_DIR}/JUCE")
else()
    include(FetchContent)
    FetchContent_Declare(JUCE
        GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
        GIT_TAG        ${WT_JUCE_TAG}
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(JUCE)
endif()

# Source files are UTF-8 (Spanish texts)
if(MSVC)
    add_compile_options(/utf-8)
endif()

# ------------------------------------------------------------------------------
# Embedded resources (format templates)
# ------------------------------------------------------------------------------
file(GLOB WT_TEMPLATE_FILES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/templates/adv/*.xml"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/templates/nki/*.xml"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/templates/mpc/*.xpm"
    "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_256.png")

juce_add_binary_data(WavethingsBinaryData
    NAMESPACE BinaryData
    HEADER_NAME BinaryData.h
    SOURCES ${WT_TEMPLATE_FILES})

# ------------------------------------------------------------------------------
# Core library: everything except the GUI, shared by the app and the tests
# ------------------------------------------------------------------------------
set(WT_CORE_SOURCES
    Source/Core/NoteUtils.cpp
    Source/Core/SampleProcessing.cpp
    Source/Core/WavFile.cpp
    Source/Core/AudioEngine.cpp
    Source/Core/MidiLearn.cpp
    Source/Core/SamplingJob.cpp
    Source/Core/Settings.cpp
    Source/Core/PluginScanner.cpp
    Source/Export/Exporter.cpp
    Source/Export/SfzExporter.cpp
    Source/Export/DecentSamplerExporter.cpp
    Source/Export/BitwigExporter.cpp
    Source/Export/AbletonExporter.cpp
    Source/Export/MpcExporter.cpp
    Source/Export/MpcModernExporter.cpp
    Source/Export/ExsExporter.cpp
    Source/Export/KontaktExporter.cpp
    Source/Export/Sf2Exporter.cpp
    Source/Export/MossBridgeExporter.cpp
    Source/Export/Templates.cpp)

set(WT_PLUGIN_HOST_DEFINITIONS
    WT_VERSION_STRING="${PROJECT_VERSION}"
    JUCE_PLUGINHOST_VST3=1
    JUCE_PLUGINHOST_VST=0
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    JUCE_STRICT_REFCOUNTEDPOINTER=1)

if(APPLE)
    list(APPEND WT_PLUGIN_HOST_DEFINITIONS JUCE_PLUGINHOST_AU=1)
endif()
if(UNIX AND NOT APPLE)
    list(APPEND WT_PLUGIN_HOST_DEFINITIONS JUCE_PLUGINHOST_LV2=1 JUCE_JACK=1)
endif()

# ------------------------------------------------------------------------------
# The application
# ------------------------------------------------------------------------------
if(WT_BUILD_APP)
juce_add_gui_app(WavethingsAutosampler
    PRODUCT_NAME "Wavethings Autosampler"
    COMPANY_NAME "Wavethings"
    BUNDLE_ID "com.wavethings.autosampler"
    ICON_BIG "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_1024.png"
    ICON_SMALL "${CMAKE_CURRENT_SOURCE_DIR}/Resources/icon/icon_256.png"
    VERSION ${PROJECT_VERSION}
    MICROPHONE_PERMISSION_ENABLED TRUE
    MICROPHONE_PERMISSION_TEXT "Wavethings Autosampler necesita acceso al audio para grabar instrumentos.")

target_sources(WavethingsAutosampler PRIVATE
    ${WT_CORE_SOURCES}
    Source/Main.cpp
    Source/MainComponent.cpp
    Source/UI/WavethingsLookAndFeel.cpp
    Source/UI/LevelMeter.cpp
    Source/UI/WaveformView.cpp
    Source/UI/MappedControlsPanel.cpp
    Source/UI/PluginWindow.cpp
    Source/UI/RangeWizard.cpp)

target_include_directories(WavethingsAutosampler PRIVATE Source)

target_compile_definitions(WavethingsAutosampler PRIVATE
    ${WT_PLUGIN_HOST_DEFINITIONS}
    JUCE_APPLICATION_NAME_STRING="$<TARGET_PROPERTY:WavethingsAutosampler,JUCE_PRODUCT_NAME>"
    JUCE_APPLICATION_VERSION_STRING="$<TARGET_PROPERTY:WavethingsAutosampler,JUCE_VERSION>")

target_link_libraries(WavethingsAutosampler
    PRIVATE
        WavethingsBinaryData
        juce::juce_audio_utils
        juce::juce_audio_processors
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
endif()

# ------------------------------------------------------------------------------
# Console test runner (sample processing + all exporters, no audio device)
# ------------------------------------------------------------------------------
if(WT_BUILD_TESTS)
    juce_add_console_app(WavethingsTests PRODUCT_NAME "WavethingsTests")
    target_sources(WavethingsTests PRIVATE ${WT_CORE_SOURCES} Tests/TestMain.cpp)
    target_include_directories(WavethingsTests PRIVATE Source)
    target_compile_definitions(WavethingsTests PRIVATE
        ${WT_PLUGIN_HOST_DEFINITIONS}
        JUCE_MODAL_LOOPS_PERMITTED=1)
    target_link_libraries(WavethingsTests
        PRIVATE
            WavethingsBinaryData
            juce::juce_audio_utils
            juce::juce_audio_processors
            juce::juce_dsp
        PUBLIC
            juce::juce_recommended_config_flags
            juce::juce_recommended_warning_flags)
endif()
