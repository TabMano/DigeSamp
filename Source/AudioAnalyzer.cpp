#include "AudioAnalyzer.h"
#include <cmath>
#include <algorithm>

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
        logFile.appendText (line + juce::newLine);
    }
}

namespace
{
    constexpr std::array<float, 12> majorProfile = {
        6.35f, 2.23f, 3.48f, 2.33f, 4.38f, 4.09f, 2.52f, 5.19f, 2.39f, 3.66f, 2.29f, 2.88f
    };
    constexpr std::array<float, 12> minorProfile = {
        6.33f, 2.68f, 3.52f, 5.38f, 2.60f, 3.53f, 2.54f, 4.75f, 3.98f, 2.69f, 3.34f, 3.17f
    };

    const juce::StringArray noteNames = { "C", "C#", "D", "D#", "E", "F",
                                           "F#", "G", "G#", "A", "A#", "B" };

    float correlate (const std::array<float, 12>& a, const std::array<float, 12>& b)
    {
        float meanA = 0.0f, meanB = 0.0f;
        for (int i = 0; i < 12; ++i) { meanA += a[i]; meanB += b[i]; }
        meanA /= 12.0f; meanB /= 12.0f;

        float num = 0.0f, denA = 0.0f, denB = 0.0f;
        for (int i = 0; i < 12; ++i)
        {
            float da = a[i] - meanA;
            float db = b[i] - meanB;
            num += da * db;
            denA += da * da;
            denB += db * db;
        }
        auto denom = std::sqrt (denA * denB);
        return denom > 1.0e-9f ? num / denom : 0.0f;
    }

    inline bool isCancelled (const std::function<bool()>& shouldCancel)
    {
        return shouldCancel && shouldCancel();
    }
}

AnalysisResult AudioAnalyzer::analyze (const juce::AudioBuffer<float>& buffer, double sampleRate,
                                        const std::function<bool()>& shouldCancel)
{
    digeSampLog ("AudioAnalyzer::analyze() entered");
    digeSampLog ("Input samples: " + juce::String (buffer.getNumSamples()));
    digeSampLog ("Input channels: " + juce::String (buffer.getNumChannels()));
    digeSampLog ("Sample rate: " + juce::String (sampleRate));

    AnalysisResult result;

    if (buffer.getNumSamples() == 0 || sampleRate <= 0.0)
    {
        digeSampLog ("ERROR: No audio data to analyze");
        result.errorMessage = "No audio data to analyze";
        return result;
    }

    digeSampLog ("Converting input to mono...");
    auto mono = toMono (buffer);
    digeSampLog ("Mono conversion complete");

    if (isCancelled (shouldCancel)) { result.cancelled = true; result.errorMessage = "Cancelled"; return result; }

    digeSampLog ("Computing onset envelope...");
    auto onsetEnvelope = computeOnsetEnvelope (mono, shouldCancel);
    digeSampLog ("Onset envelope complete; frames: " + juce::String ((int) onsetEnvelope.size()));
    if (isCancelled (shouldCancel)) { result.cancelled = true; result.errorMessage = "Cancelled"; return result; }

    auto frameRate = sampleRate / (double) onsetHopSize;
    digeSampLog ("Estimating BPM...");
    auto [bpm, bpmConf] = estimateBpmRobust (onsetEnvelope, frameRate, shouldCancel);
    digeSampLog ("BPM estimation complete: " + juce::String (bpm)
                 + " confidence=" + juce::String (bpmConf));
    if (isCancelled (shouldCancel)) { result.cancelled = true; result.errorMessage = "Cancelled"; return result; }

    result.bpm = bpm;
    result.bpmConfidence = bpmConf;

    digeSampLog ("Estimating key...");
    auto [keyName, keyConf] = estimateKey (mono, sampleRate, shouldCancel);
    digeSampLog ("Key estimation complete: " + keyName
                 + " confidence=" + juce::String (keyConf));
    if (isCancelled (shouldCancel)) { result.cancelled = true; result.errorMessage = "Cancelled"; return result; }

    result.keyName = keyName;
    result.keyConfidence = keyConf;

    if (bpm <= 0.0)
        result.errorMessage = "Couldn't lock onto a clear tempo - try a longer or more rhythmic sample";

    result.success = bpm > 0.0;
    digeSampLog ("AudioAnalyzer::analyze() exiting; success="
                 + juce::String (result.success ? "YES" : "NO"));
    return result;
}

std::vector<float> AudioAnalyzer::toMono (const juce::AudioBuffer<float>& buffer)
{
    digeSampLog ("toMono() entered");
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();
    std::vector<float> mono (static_cast<size_t> (numSamples), 0.0f);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getReadPointer (ch);
        for (int i = 0; i < numSamples; ++i)
            mono[static_cast<size_t> (i)] += data[i];
    }

    if (numChannels > 1)
        for (auto& s : mono)
            s /= (float) numChannels;

    digeSampLog ("toMono() complete");
    return mono;
}

// ============================== BPM ==============================

std::vector<float> AudioAnalyzer::computeOnsetEnvelope (const std::vector<float>& mono,
                                                         const std::function<bool()>& shouldCancel)
{
    digeSampLog ("computeOnsetEnvelope() entered");
    juce::dsp::FFT fft (onsetFftOrder);
    juce::dsp::WindowingFunction<float> window (onsetFftSize, juce::dsp::WindowingFunction<float>::hann);

    const int numBins = onsetFftSize / 2;
    std::vector<float> prevMagnitude (static_cast<size_t> (numBins), 0.0f);
    std::vector<float> onsetEnvelope;

    std::vector<float> fftData (static_cast<size_t> (onsetFftSize * 2), 0.0f);

    size_t pos = 0;
    while (pos + static_cast<size_t> (onsetFftSize) <= mono.size())
    {
        if (isCancelled (shouldCancel))
            break; // caller re-checks and discards the partial result

        std::fill (fftData.begin(), fftData.end(), 0.0f);
        std::copy (mono.begin() + static_cast<long> (pos),
                   mono.begin() + static_cast<long> (pos) + onsetFftSize,
                   fftData.begin());

        window.multiplyWithWindowingTable (fftData.data(), onsetFftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        float flux = 0.0f;
        for (int bin = 0; bin < numBins; ++bin)
        {
            float diff = fftData[static_cast<size_t> (bin)] - prevMagnitude[static_cast<size_t> (bin)];
            if (diff > 0.0f)
                flux += diff;
            prevMagnitude[static_cast<size_t> (bin)] = fftData[static_cast<size_t> (bin)];
        }

        onsetEnvelope.push_back (flux);
        pos += static_cast<size_t> (onsetHopSize);
    }

    digeSampLog ("computeOnsetEnvelope() complete; frames="
                 + juce::String ((int) onsetEnvelope.size()));
    return onsetEnvelope;
}

AudioAnalyzer::TempoEstimate AudioAnalyzer::estimateTempoInRange (
    const std::vector<float>& onsetEnvelope, double frameRate, int startFrame, int endFrame,
    const std::function<bool()>& shouldCancel)
{
    digeSampLog ("estimateTempoInRange() start="
                 + juce::String (startFrame) + " end=" + juce::String (endFrame));
    TempoEstimate result;

    startFrame = juce::jmax (0, startFrame);
    endFrame = juce::jmin ((int) onsetEnvelope.size(), endFrame);
    int length = endFrame - startFrame;
    if (length < 8 || frameRate <= 0.0)
        return result;

    float mean = 0.0f;
    for (int i = startFrame; i < endFrame; ++i)
        mean += onsetEnvelope[(size_t) i];
    mean /= (float) length;

    std::vector<float> centered ((size_t) length);
    for (int i = 0; i < length; ++i)
        centered[(size_t) i] = onsetEnvelope[(size_t) (startFrame + i)] - mean;

    int minLag = juce::jmax (1, (int) std::floor (frameRate * 60.0 / maxBpm));
    int maxLag = juce::jmin (length - 1, (int) std::ceil (frameRate * 60.0 / minBpm));
    if (minLag >= maxLag)
        return result;

    int acMaxLag = juce::jmin (length - 1, maxLag * 3 + 1);
    std::vector<double> ac ((size_t) acMaxLag + 1, 0.0);
    for (int lag = minLag; lag <= acMaxLag; ++lag)
    {
        if (isCancelled (shouldCancel))
            return result; // bail with the default (zero) estimate

        double sum = 0.0;
        int count = length - lag;
        for (int i = 0; i < count; ++i)
            sum += (double) centered[(size_t) i] * (double) centered[(size_t) (i + lag)];
        ac[(size_t) lag] = sum / (double) juce::jmax (1, count);
    }

    double bestScore = -1.0e300;
    int bestLag = minLag;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double score = ac[(size_t) lag];
        if (2 * lag <= acMaxLag) score += 0.6 * ac[(size_t) (2 * lag)];
        if (3 * lag <= acMaxLag) score += 0.35 * ac[(size_t) (3 * lag)];

        if (score > bestScore)
        {
            bestScore = score;
            bestLag = lag;
        }
    }

    double bpm = 60.0 * frameRate / (double) bestLag;
    while (bpm < minBpm) bpm *= 2.0;
    while (bpm > maxBpm) bpm /= 2.0;

    result.bpm = bpm;
    result.score = (float) juce::jmax (0.0, bestScore);
    digeSampLog ("estimateTempoInRange() result BPM="
                 + juce::String (result.bpm) + " score=" + juce::String (result.score));
    return result;
}

std::pair<double, float> AudioAnalyzer::estimateBpmRobust (const std::vector<float>& onsetEnvelope, double frameRate,
                                                             const std::function<bool()>& shouldCancel)
{
    digeSampLog ("estimateBpmRobust() entered; frames="
                 + juce::String ((int) onsetEnvelope.size()));
    if (onsetEnvelope.size() < 8 || frameRate <= 0.0)
        return { 0.0, 0.0f };

    double totalSeconds = (double) onsetEnvelope.size() / frameRate;
    std::vector<TempoEstimate> estimates;

    if (totalSeconds <= segmentSeconds)
    {
        auto e = estimateTempoInRange (onsetEnvelope, frameRate, 0, (int) onsetEnvelope.size(), shouldCancel);
        if (e.bpm > 0.0)
            estimates.push_back (e);
    }
    else
    {
        int segFrames = (int) std::round (segmentSeconds * frameRate);
        int hopFrames = (int) std::round (segmentHopSeconds * frameRate);

        for (int start = 0; start + segFrames <= (int) onsetEnvelope.size(); start += hopFrames)
        {
            if (isCancelled (shouldCancel))
                return { 0.0, 0.0f };

            auto e = estimateTempoInRange (onsetEnvelope, frameRate, start, start + segFrames, shouldCancel);
            if (e.bpm > 0.0)
                estimates.push_back (e);
        }

        int lastStart = (int) onsetEnvelope.size() - segFrames;
        if (lastStart > 0 && ! isCancelled (shouldCancel))
        {
            auto e = estimateTempoInRange (onsetEnvelope, frameRate, lastStart, (int) onsetEnvelope.size(), shouldCancel);
            if (e.bpm > 0.0)
                estimates.push_back (e);
        }
    }

    if (estimates.empty())
        return { 0.0, 0.0f };

    double totalWeight = 0.0;
    for (auto& e : estimates)
        totalWeight += e.score;

    if (totalWeight <= 0.0)
    {
        for (auto& e : estimates)
            e.score = 1.0f;
        totalWeight = (double) estimates.size();
    }

    double bestClusterWeight = -1.0;
    double bestClusterBpm = estimates.front().bpm;

    for (auto& candidate : estimates)
    {
        double weightSum = 0.0;
        double weightedBpmSum = 0.0;

        for (auto& other : estimates)
        {
            double diff       = std::abs (other.bpm - candidate.bpm);
            double diffHalf    = std::abs (other.bpm * 2.0 - candidate.bpm);
            double diffDouble  = std::abs (other.bpm - candidate.bpm * 2.0);
            double bestDiff    = std::min ({ diff, diffHalf, diffDouble });

            if (bestDiff <= tempoVoteToleranceBpm)
            {
                weightSum += other.score;
                weightedBpmSum += other.score * other.bpm;
            }
        }

        if (weightSum > bestClusterWeight)
        {
            bestClusterWeight = weightSum;
            bestClusterBpm = weightedBpmSum / weightSum;
        }
    }

    float confidence = (float) juce::jlimit (0.0, 1.0, bestClusterWeight / totalWeight);
    const auto roundedBpm = std::round (bestClusterBpm * 10.0) / 10.0;
    digeSampLog ("estimateBpmRobust() complete; BPM="
                 + juce::String (roundedBpm) + " confidence=" + juce::String (confidence));
    return { roundedBpm, confidence };
}

// ============================== KEY ==============================

std::array<float, 12> AudioAnalyzer::computeChromaVector (const std::vector<float>& mono, double sampleRate,
                                                           const std::function<bool()>& shouldCancel)
{
    digeSampLog ("computeChromaVector() entered");
    std::array<float, 12> chroma {};
    chroma.fill (0.0f);

    if ((int) mono.size() < keyFftSize)
        return chroma;

    juce::dsp::FFT fft (keyFftOrder);
    juce::dsp::WindowingFunction<float> window (keyFftSize, juce::dsp::WindowingFunction<float>::hann);

    std::vector<float> fftData (static_cast<size_t> (keyFftSize * 2), 0.0f);
    const int numBins = keyFftSize / 2;

    size_t pos = 0;
    int framesProcessed = 0;

    while (pos + static_cast<size_t> (keyFftSize) <= mono.size())
    {
        if (isCancelled (shouldCancel))
            break;

        std::fill (fftData.begin(), fftData.end(), 0.0f);
        std::copy (mono.begin() + static_cast<long> (pos),
                   mono.begin() + static_cast<long> (pos) + keyFftSize,
                   fftData.begin());

        window.multiplyWithWindowingTable (fftData.data(), keyFftSize);
        fft.performFrequencyOnlyForwardTransform (fftData.data());

        for (int bin = 2; bin < numBins; ++bin)
        {
            double freq = (double) bin * sampleRate / (double) keyFftSize;
            if (freq < 60.0 || freq > 5000.0)
                continue;

            double midi = 69.0 + 12.0 * std::log2 (freq / 440.0);
            int pitchClass = ((int) std::lround (midi)) % 12;
            if (pitchClass < 0) pitchClass += 12;

            chroma[static_cast<size_t> (pitchClass)] += (float) std::log1p ((double) fftData[static_cast<size_t> (bin)]);
        }

        ++framesProcessed;
        pos += static_cast<size_t> (keyHopSize);
    }

    if (framesProcessed > 0)
        for (auto& c : chroma)
            c /= (float) framesProcessed;

    digeSampLog ("computeChromaVector() complete; frames="
                 + juce::String (framesProcessed));
    return chroma;
}

std::pair<juce::String, float> AudioAnalyzer::estimateKey (const std::vector<float>& mono, double sampleRate,
                                                            const std::function<bool()>& shouldCancel)
{
    digeSampLog ("estimateKey() entered");
    auto chroma = computeChromaVector (mono, sampleRate, shouldCancel);

    if (isCancelled (shouldCancel))
        return { "Unknown", 0.0f };

    bool allZero = std::all_of (chroma.begin(), chroma.end(), [] (float c) { return c == 0.0f; });
    if (allZero)
        return { "Unknown", 0.0f };

    struct Candidate { float score; int tonic; bool isMajor; };
    std::vector<Candidate> candidates;
    candidates.reserve (24);

    for (int tonic = 0; tonic < 12; ++tonic)
    {
        std::array<float, 12> rotatedMajor {}, rotatedMinor {};
        for (int i = 0; i < 12; ++i)
        {
            rotatedMajor[(size_t) ((i + tonic) % 12)] = majorProfile[(size_t) i];
            rotatedMinor[(size_t) ((i + tonic) % 12)] = minorProfile[(size_t) i];
        }

        candidates.push_back ({ correlate (chroma, rotatedMajor), tonic, true });
        candidates.push_back ({ correlate (chroma, rotatedMinor), tonic, false });
    }

    std::sort (candidates.begin(), candidates.end(),
               [] (const Candidate& a, const Candidate& b) { return a.score > b.score; });

    const auto& best = candidates.front();
    float margin = candidates.size() > 1 ? (best.score - candidates[1].score) : 0.0f;

    float confidence = juce::jlimit (0.0f, 1.0f,
        0.5f * juce::jlimit (0.0f, 1.0f, best.score) + 0.5f * juce::jlimit (0.0f, 1.0f, margin * 4.0f));

    juce::String name = noteNames[best.tonic] + (best.isMajor ? " Major" : " Minor");
    digeSampLog ("estimateKey() complete; key=" + name
                 + " confidence=" + juce::String (confidence));
    return { name, confidence };
}
