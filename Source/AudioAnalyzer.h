#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <utility>
#include <functional>

/** Result of analyzing one audio buffer. */
struct AnalysisResult
{
    double bpm = 0.0;
    juce::String keyName = "Unknown";

    float bpmConfidence = 0.0f;
    float keyConfidence = 0.0f;

    bool success = false;
    bool cancelled = false;    // true if shouldCancel() fired mid-analysis
    juce::String errorMessage;
};

/**
    Offline analyzer estimating tempo and key from a full audio buffer.

    Cancellation: analyze() and its helpers accept an optional shouldCancel
    callback, checked periodically between (not inside) FFT frames and
    autocorrelation lags. This exists so a plugin shutting down mid-analysis
    can bail out in roughly one frame's worth of work instead of running the
    full pass to completion - important because the background thread that
    calls this is joined (Thread::stopThread) during plugin destruction, and
    a host waiting on that join is a host that appears frozen.
*/
class AudioAnalyzer
{
public:
    AnalysisResult analyze (const juce::AudioBuffer<float>& buffer, double sampleRate,
                             const std::function<bool()>& shouldCancel = {});

private:
    std::vector<float> toMono (const juce::AudioBuffer<float>& buffer);

    // --- BPM ---
    std::vector<float> computeOnsetEnvelope (const std::vector<float>& mono,
                                              const std::function<bool()>& shouldCancel);

    struct TempoEstimate { double bpm = 0.0; float score = 0.0f; };
    TempoEstimate estimateTempoInRange (const std::vector<float>& onsetEnvelope, double frameRate,
                                         int startFrame, int endFrame,
                                         const std::function<bool()>& shouldCancel);
    std::pair<double, float> estimateBpmRobust (const std::vector<float>& onsetEnvelope, double frameRate,
                                                 const std::function<bool()>& shouldCancel);

    // --- Key ---
    std::pair<juce::String, float> estimateKey (const std::vector<float>& mono, double sampleRate,
                                                 const std::function<bool()>& shouldCancel);
    std::array<float, 12> computeChromaVector (const std::vector<float>& mono, double sampleRate,
                                                const std::function<bool()>& shouldCancel);

    // Onset detection: prioritizes time resolution.
    static constexpr int onsetFftOrder = 11;         // 2048 samples
    static constexpr int onsetFftSize  = 1 << onsetFftOrder;
    static constexpr int onsetHopSize  = 512;        // 75% overlap

    // Key/chroma detection: prioritizes frequency resolution (low notes need it).
    static constexpr int keyFftOrder = 13;           // 8192 samples
    static constexpr int keyFftSize  = 1 << keyFftOrder;
    static constexpr int keyHopSize  = keyFftSize / 2;

    static constexpr double minBpm = 60.0;
    static constexpr double maxBpm = 200.0;

    static constexpr double segmentSeconds    = 20.0; // per-segment tempo analysis window
    static constexpr double segmentHopSeconds = 10.0; // 50% overlap between segments
    static constexpr double tempoVoteToleranceBpm = 3.0;
};
