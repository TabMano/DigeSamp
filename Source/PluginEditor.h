#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BpmKeyDetectorEditor : public juce::AudioProcessorEditor,
                              private juce::Timer
{
public:
    explicit BpmKeyDetectorEditor (BpmKeyDetectorProcessor&);
    ~BpmKeyDetectorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void openFileChooser();

    BpmKeyDetectorProcessor& processor;

    juce::Label statusLabel;   // shows chosen filename or "No file selected"
    juce::TextButton browseButton { "Choose File..." };
    juce::Label resultLabel;   // shows "128.0 BPM  |  F# Minor" or an error message
    juce::Label creditLabel;

    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmKeyDetectorEditor)
};
