#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class BpmKeyDetectorEditor : public juce::AudioProcessorEditor,
                              public juce::FileDragAndDropTarget,
                              private juce::Timer
{
public:
    explicit BpmKeyDetectorEditor (BpmKeyDetectorProcessor&);
    ~BpmKeyDetectorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;

private:
    void timerCallback() override;

    BpmKeyDetectorProcessor& processor;

    juce::Label dropZoneLabel;
    juce::Label resultLabel; // shows "128.0 BPM  ·  F# Minor" or an error/status message
    juce::Label creditLabel;

    bool isDragOver = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmKeyDetectorEditor)
};
