#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "AudioAnalyzer.h"

/**
    Deliberately simple: this plugin only detects. It doesn't process audio,
    doesn't listen live, doesn't save state - you drop a file on it, it
    analyzes that file on a background thread, and shows the result.
*/
class BpmKeyDetectorProcessor : public juce::AudioProcessor
{
public:
    BpmKeyDetectorProcessor();
    ~BpmKeyDetectorProcessor() override;

    void prepareToPlay (double, int) override {}
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {} // pass-through, does nothing

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    /** Queues a file for background analysis (called by the drag-and-drop UI). */
    void analyzeFile (const juce::File& file);

    /** Thread-safe snapshot of the latest result (call from the editor's Timer). */
    AnalysisResult getLastResult() const;

    /** True while a background analysis pass is running. */
    bool isAnalyzing() const { return analyzing.load(); }

private:
    struct AnalysisThread : public juce::Thread
    {
        explicit AnalysisThread (BpmKeyDetectorProcessor& ownerIn)
            : juce::Thread ("BpmKeyDetector Analysis"), owner (ownerIn) {}

        void run() override;
        BpmKeyDetectorProcessor& owner;
    };

    juce::AudioFormatManager formatManager;
    AudioAnalyzer analyzer;
    AnalysisThread analysisThread;

    juce::CriticalSection fileRequestLock;
    juce::File pendingFile;
    bool hasPendingFileRequest = false;

    mutable juce::CriticalSection resultLock;
    AnalysisResult lastResult;
    std::atomic<bool> analyzing { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (BpmKeyDetectorProcessor)
};
