#include "PluginProcessor.h"
#include "PluginEditor.h"

BpmKeyDetectorProcessor::BpmKeyDetectorProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      analysisThread (*this)
{
    // WAV/AIFF always; FLAC/OGG via the JUCE_USE_FLAC / JUCE_USE_OGGVORBIS
    // compile definitions in CMakeLists.txt; MP3 and platform extras
    // (M4A/AAC on macOS, WMA on Windows) are added automatically too.
    // See README.md for the full format matrix.
    formatManager.registerBasicFormats();

    analysisThread.startThread();
}

BpmKeyDetectorProcessor::~BpmKeyDetectorProcessor()
{
    analysisThread.signalThreadShouldExit();
    analysisThread.notify();
    analysisThread.stopThread (4000);
}

bool BpmKeyDetectorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

juce::AudioProcessorEditor* BpmKeyDetectorProcessor::createEditor()
{
    return new BpmKeyDetectorEditor (*this);
}

void BpmKeyDetectorProcessor::analyzeFile (const juce::File& file)
{
    const juce::ScopedLock sl (fileRequestLock);
    pendingFile = file;
    hasPendingFileRequest = true;
    analysisThread.notify(); // wake the thread immediately
}

AnalysisResult BpmKeyDetectorProcessor::getLastResult() const
{
    const juce::ScopedLock sl (resultLock);
    return lastResult;
}

void BpmKeyDetectorProcessor::AnalysisThread::run()
{
    while (! threadShouldExit())
    {
        juce::File fileToAnalyze;
        {
            const juce::ScopedLock sl (owner.fileRequestLock);
            if (owner.hasPendingFileRequest)
            {
                fileToAnalyze = owner.pendingFile;
                owner.hasPendingFileRequest = false;
            }
        }

        if (fileToAnalyze != juce::File())
        {
            owner.analyzing = true;
            AnalysisResult result;

            std::unique_ptr<juce::AudioFormatReader> reader (
                owner.formatManager.createReaderFor (fileToAnalyze));

            if (reader == nullptr)
            {
                result.errorMessage = "Unsupported or unreadable file format";
            }
            else if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
            {
                result.errorMessage = "File has no readable audio data";
            }
            else
            {
                juce::AudioBuffer<float> buffer (
                    (int) reader->numChannels,
                    (int) juce::jmin<juce::int64> (reader->lengthInSamples,
                                                     (juce::int64) reader->sampleRate * 60 * 10)); // cap at 10 min

                reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);

                if (! threadShouldExit())
                    result = owner.analyzer.analyze (buffer, reader->sampleRate);
            }

            {
                const juce::ScopedLock sl (owner.resultLock);
                owner.lastResult = result;
            }
            owner.analyzing = false;
        }
        else
        {
            wait (100); // idle - analyzeFile() wakes us early via notify()
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BpmKeyDetectorProcessor();
}
