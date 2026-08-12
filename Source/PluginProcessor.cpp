#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
    juce::File getDigeSampLogFile()
    {
        return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("DigeSamp.log");
    }

    void digeSampLog (const juce::String& message)
    {
        const auto line = "[" + juce::Time::getCurrentTime().toString (true, true)
                        + "] " + message;

        DBG (line);

        auto logFile = getDigeSampLogFile();
        logFile.appendText (line + juce::newLine,
                            false,
                            false,
                            "\n",
                            "\r");
    }
}

BpmKeyDetectorProcessor::BpmKeyDetectorProcessor()
    : AudioProcessor (BusesProperties()
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      analysisThread (*this)
{
    digeSampLog ("Processor constructed");

    // WAV/AIFF always; FLAC/OGG via the JUCE_USE_FLAC / JUCE_USE_OGGVORBIS
    // compile definitions in CMakeLists.txt; MP3 and platform extras
    // (M4A/AAC on macOS, WMA on Windows) are added automatically too.
    // See README.md for the full format matrix.
    formatManager.registerBasicFormats();

    analysisThread.startThread();
    digeSampLog ("Analysis thread started");
}

BpmKeyDetectorProcessor::~BpmKeyDetectorProcessor()
{
    digeSampLog ("Processor destructor started");
    digeSampLog ("Requesting analysis thread shutdown");
    analysisThread.signalThreadShouldExit();
    analysisThread.notify();
    digeSampLog ("Waiting for analysis thread...");
    const bool stopped = analysisThread.stopThread (4000);
    digeSampLog ("Analysis thread stopped: " + juce::String (stopped ? "YES" : "NO"));
    digeSampLog ("Processor destructor finished");
}

bool BpmKeyDetectorProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        || layouts.getMainOutputChannelSet() == juce::AudioChannelSet::mono();
}

juce::AudioProcessorEditor* BpmKeyDetectorProcessor::createEditor()
{
    digeSampLog ("createEditor() called");
    return new BpmKeyDetectorEditor (*this);
}

void BpmKeyDetectorProcessor::analyzeFile (const juce::File& file)
{
    digeSampLog ("analyzeFile() called");
    digeSampLog ("File: " + file.getFullPathName());
    digeSampLog ("Exists: " + juce::String (file.existsAsFile() ? "YES" : "NO"));
    if (file.existsAsFile())
        digeSampLog ("File size: " + juce::String (file.getSize()) + " bytes");

    const juce::ScopedLock sl (fileRequestLock);
    pendingFile = file;
    hasPendingFileRequest = true;
    digeSampLog ("Pending file request set");
    analysisThread.notify(); // wake the thread immediately
    digeSampLog ("Analysis thread notified");
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
            digeSampLog ("Analysis thread received file");
            digeSampLog ("File: " + fileToAnalyze.getFullPathName());
            digeSampLog ("Thread exit requested: " + juce::String (threadShouldExit() ? "YES" : "NO"));

            owner.analyzing = true;
            digeSampLog ("analyzing flag set: YES");
            AnalysisResult result;

            digeSampLog ("Creating AudioFormatReader...");
            std::unique_ptr<juce::AudioFormatReader> reader (
                owner.formatManager.createReaderFor (fileToAnalyze));

            if (reader == nullptr)
            {
                digeSampLog ("ERROR: AudioFormatReader creation failed");
                result.errorMessage = "Unsupported or unreadable file format";
            }
            else if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
            {
                digeSampLog ("ERROR: Reader has invalid audio properties");
                result.errorMessage = "File has no readable audio data";
            }
            else
            {
                digeSampLog ("AudioFormatReader created");
                digeSampLog ("Sample rate: " + juce::String (reader->sampleRate));
                digeSampLog ("Channels: " + juce::String ((int) reader->numChannels));
                digeSampLog ("Total samples: " + juce::String (reader->lengthInSamples));

                // Cap how much we read to bound worst-case memory (a 10-min
                // stereo file at 44.1kHz would be ~210MB just for the raw
                // read buffer). 3 minutes covers the vast majority of music
                // and loop content; the segment-voting BPM algorithm doesn't
                // need the whole track anyway. Longer files just get their
                // first 3 minutes analyzed rather than being rejected -
                // representative-chunk sampling for very long files is a
                // reasonable next step if that ever matters in practice.
                constexpr juce::int64 maxSecondsToRead = 180;
                const auto samplesToRead = juce::jmin<juce::int64> (
                    reader->lengthInSamples,
                    (juce::int64) reader->sampleRate * maxSecondsToRead);

                digeSampLog ("Allocating audio buffer");
                digeSampLog ("Samples to read: " + juce::String (samplesToRead));
                digeSampLog ("Estimated raw buffer memory: "
                             + juce::String ((double) reader->numChannels
                                             * (double) samplesToRead
                                             * sizeof (float) / (1024.0 * 1024.0), 1)
                             + " MB");

                juce::AudioBuffer<float> buffer (
                    (int) reader->numChannels,
                    (int) samplesToRead);

                digeSampLog ("Audio buffer allocated");
                digeSampLog ("Reading audio into buffer...");
                const bool readOk = reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, true);
                digeSampLog ("Audio read completed: " + juce::String (readOk ? "YES" : "NO"));

                if (! readOk)
                    result.errorMessage = "Could not read audio data";

                if (! threadShouldExit() && result.errorMessage.isEmpty())
                    result = owner.analyzer.analyze (buffer, reader->sampleRate,
                                                       [this] { return threadShouldExit(); });
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
