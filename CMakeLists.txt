cmake_minimum_required(VERSION 3.22)
project(AuroraMeters VERSION 0.1.1 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()
if(MSVC OR CMAKE_CXX_COMPILER_FRONTEND_VARIANT STREQUAL "MSVC")
    # Set this before creating JUCE or Aurora targets so all linked objects use
    # the same runtime. Release downloads do not need the VC++ redistributable.
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
    add_compile_options("$<$<COMPILE_LANGUAGE:C,CXX>:/utf-8>")
endif()

set(AURORA_JUCE_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/JUCE"
    CACHE PATH "Path to the pinned JUCE 8.0.6 source")
option(AURORA_BUILD_GUI_TESTS "Build interactive preview and graphical QA applications" ON)
add_subdirectory("${AURORA_JUCE_SOURCE_DIR}" third_party/JUCE)

set(aurora_plugin_formats VST3)
if(APPLE)
    list(PREPEND aurora_plugin_formats AU)
endif()
juce_add_plugin(Aurora
    COMPANY_NAME "Soft Club"
    BUNDLE_ID "audio.softclub.aurora"
    PLUGIN_MANUFACTURER_CODE Aucl
    PLUGIN_CODE Auro
    FORMATS ${aurora_plugin_formats}
    PRODUCT_NAME "Aurora"
    DESCRIPTION "Audio metering and reactive visuals"
    IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS FALSE
    COPY_PLUGIN_AFTER_BUILD FALSE
    VST3_CATEGORIES Fx Analyzer
    AU_MAIN_TYPE kAudioUnitType_Effect)
juce_generate_juce_header(Aurora)
target_sources(Aurora PRIVATE Source/Analysis.cpp Source/PluginProcessor.cpp Source/PluginEditor.cpp Source/VisualSurface.cpp Source/CustomizationPanel.cpp Source/FactoryPresets.cpp Source/FactoryPresetBrowser.cpp)
target_compile_definitions(Aurora PUBLIC JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0 JUCE_VST3_CAN_REPLACE_VST2=0)
target_link_libraries(Aurora PUBLIC juce::juce_audio_utils juce::juce_opengl PRIVATE juce::juce_recommended_config_flags)
target_include_directories(Aurora PUBLIC Source $<TARGET_PROPERTY:Aurora,JUCE_GENERATED_SOURCES_DIRECTORY>)

add_executable(AnalysisTests Tests/AnalysisTests.cpp Source/Analysis.cpp)
target_include_directories(AnalysisTests PRIVATE Source)
enable_testing()
add_test(NAME analysis COMMAND AnalysisTests)

add_executable(VisualFramingTests Tests/VisualFramingTests.cpp)
target_include_directories(VisualFramingTests PRIVATE Source)
add_test(NAME visual_framing COMMAND VisualFramingTests)

add_executable(VisualReactivityTests Tests/VisualReactivityTests.cpp Source/Analysis.cpp)
target_include_directories(VisualReactivityTests PRIVATE Source)
add_test(NAME visual_reactivity COMMAND VisualReactivityTests)

add_executable(WaveformClockTests Tests/WaveformClockTests.cpp)
target_include_directories(WaveformClockTests PRIVATE Source)
add_test(NAME waveform_clock COMMAND WaveformClockTests)

add_executable(WaveformProjectionTests Tests/WaveformProjectionTests.cpp)
target_include_directories(WaveformProjectionTests PRIVATE Source)
add_test(NAME waveform_projection COMMAND WaveformProjectionTests)

# CPU raster/cache integration tests: deterministic software images, no window,
# audio device, or OpenGL context required.
juce_add_console_app(WaveformRibbonTests PRODUCT_NAME "Aurora Waveform Ribbon Tests")
juce_generate_juce_header(WaveformRibbonTests)
target_sources(WaveformRibbonTests PRIVATE Tests/WaveformRibbonTests.cpp)
target_include_directories(WaveformRibbonTests PRIVATE Source)
target_link_libraries(WaveformRibbonTests PRIVATE juce::juce_graphics juce::juce_recommended_config_flags)
add_test(NAME waveform_ribbon COMMAND WaveformRibbonTests)

juce_add_console_app(ProcessorTests PRODUCT_NAME "Aurora Processor Tests")
target_sources(ProcessorTests PRIVATE Tests/ProcessorTests.cpp)
target_link_libraries(ProcessorTests PRIVATE Aurora)
add_test(NAME processor COMMAND ProcessorTests)

juce_add_console_app(EditorPerformanceTests PRODUCT_NAME "Aurora Editor Performance Tests")
target_sources(EditorPerformanceTests PRIVATE Tests/EditorPerformanceTests.cpp)
target_link_libraries(EditorPerformanceTests PRIVATE Aurora)
add_test(NAME editor_performance COMMAND EditorPerformanceTests)

# These applications require a graphical desktop and are not CTest tests.
# Keep them available for native UI/GPU validation, but omit them in headless CI.
if(AURORA_BUILD_GUI_TESTS)
juce_add_gui_app(AuroraPreview PRODUCT_NAME "Aurora Preview" BUNDLE_ID "audio.softclub.aurora.preview")
target_sources(AuroraPreview PRIVATE Tests/Preview.cpp)
target_link_libraries(AuroraPreview PRIVATE Aurora)

# Explicit native waveform/UI QA runner. Opens its own window, uses synthetic
# PCM with no audio device, writes component PNGs/report under /tmp, then exits.
juce_add_gui_app(AuroraWaveformTests PRODUCT_NAME "Aurora Waveform Tests"
    BUNDLE_ID "audio.softclub.aurora.waveform-tests")
target_sources(AuroraWaveformTests PRIVATE Tests/WaveformRenderingTests.cpp)
target_link_libraries(AuroraWaveformTests PRIVATE Aurora)

juce_add_gui_app(AuroraCustomizationTests PRODUCT_NAME "Aurora Customization Tests"
    BUNDLE_ID "audio.softclub.aurora.customization-tests")
target_sources(AuroraCustomizationTests PRIVATE Tests/CustomizationRenderingTests.cpp)
target_link_libraries(AuroraCustomizationTests PRIVATE Aurora)

juce_add_gui_app(AuroraFactoryPresetTests PRODUCT_NAME "Aurora Factory Preset Tests"
    BUNDLE_ID "audio.softclub.aurora.factory-preset-tests")
target_sources(AuroraFactoryPresetTests PRIVATE Tests/FactoryPresetRenderingTests.cpp)
target_link_libraries(AuroraFactoryPresetTests PRIVATE Aurora)

# Explicit GPU integration runner: requires a graphical desktop, so it is not
# part of the headless CTest suite. Writes PNGs/timing JSON to /tmp/aurora-flow-qa.
juce_add_gui_app(AuroraRenderTests PRODUCT_NAME "Aurora Render Tests"
    BUNDLE_ID "audio.softclub.aurora.render-tests")
target_sources(AuroraRenderTests PRIVATE Tests/RenderingTests.cpp)
target_include_directories(AuroraRenderTests PRIVATE Source)
target_compile_definitions(AuroraRenderTests PRIVATE JUCE_WEB_BROWSER=0 JUCE_USE_CURL=0)
target_link_libraries(AuroraRenderTests PRIVATE juce::juce_gui_extra juce::juce_opengl
    juce::juce_recommended_config_flags)

# Explicit synthetic-input production-surface benchmark; requires a desktop.
juce_add_gui_app(AuroraSurfacePerformanceTests PRODUCT_NAME "Aurora Surface Performance"
    BUNDLE_ID "audio.softclub.aurora.surface-performance")
target_sources(AuroraSurfacePerformanceTests PRIVATE Tests/VisualSurfacePerformanceTests.cpp)
target_link_libraries(AuroraSurfacePerformanceTests PRIVATE Aurora)
endif()
