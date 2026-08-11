cmake_minimum_required(VERSION 3.22)

project(BpmKeyDetector VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED TRUE)

# Expects JUCE cloned as a sibling folder: ./JUCE
# (see README.md for the one-line clone command)
add_subdirectory(JUCE)

juce_add_plugin(BpmKeyDetector
    COMPANY_NAME "FourCornersK"
    PLUGIN_MANUFACTURER_CODE KRTS
    PLUGIN_CODE Bpmk
    FORMATS VST3 Standalone
    PRODUCT_NAME "BPM & Key Detector"
    IS_SYNTH FALSE
    NEEDS_MIDI_INPUT FALSE
    NEEDS_MIDI_OUTPUT FALSE
    IS_MIDI_EFFECT FALSE
    EDITOR_WANTS_KEYBOARD_FOCUS TRUE
    COPY_PLUGIN_AFTER_BUILD TRUE)

juce_generate_juce_header(BpmKeyDetector)

target_sources(BpmKeyDetector PRIVATE
    Source/PluginProcessor.cpp
    Source/PluginEditor.cpp
    Source/AudioAnalyzer.cpp)

target_compile_definitions(BpmKeyDetector PUBLIC
    JUCE_WEB_BROWSER=0
    JUCE_USE_CURL=0
    JUCE_VST3_CAN_REPLACE_VST2=0
    JUCE_DISPLAY_SPLASH_SCREEN=0
    # Widen AudioFormatManager::registerBasicFormats() beyond WAV/AIFF.
    # FLAC and OGG Vorbis decoders are bundled directly in JUCE.
    JUCE_USE_FLAC=1
    JUCE_USE_OGGVORBIS=1
    # MP3 decoding: JUCE's built-in reader is compiled in when available;
    # macOS/Windows additionally get MP3/AAC/M4A "for free" via CoreAudioFormat
    # and WindowsMediaAudioFormat, which registerBasicFormats() also adds.
    JUCE_USE_MP3AUDIOFORMAT=1)

target_link_libraries(BpmKeyDetector
    PRIVATE
        juce::juce_audio_utils
        juce::juce_audio_formats
        juce::juce_dsp
    PUBLIC
        juce::juce_recommended_config_flags
        juce::juce_recommended_lto_flags
        juce::juce_recommended_warning_flags)
