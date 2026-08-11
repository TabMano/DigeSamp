#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <vector>
#include <array>
#include <utility>

/** Result of analyzing one audio buffer. */
struct AnalysisResult
{
    double bpm = 0.0;
    juce::String keyName = "Unknown";

    // Rough 0-1 confidence scores, not just raw numbers, so the UI (and you)
    // can tell "clearly 128 BPM in F# minor" apart from "best guess, weak signal".
    float bpmConfidence = 0.0f;
    float keyConfidence = 0.0f;

    bool success = false;
    juce::String errorMessage; // set when analysis could not produce a usable result
};

/**
    Offline analyzer estimating tempo and key from a full audio buffer.

    Robustness strategy:
      - BPM: the track is split into overlapping ~20s segments, each scored
        independently with harmonic-reinforced autocorrelation (so the
        fundamental tempo period wins over its octave doubles/halves), then
        all segment estimates vote via proximity clustering. A track with
        one weak/ambient section no longer drags the whole estimate off,
        because consistent segments elsewhere outvote it.
      - Key: chroma is computed with a much larger FFT than onset detection
        needs (8192 vs 2048 samples), giving enough low-frequency resolution
        to place bass notes in the correct pitch class, and magnitudes are
        log-compressed so a handful of loud transients can't swamp sustained
        harmonic content.

    Still not a substitute for a dedicated MIR library (essentia/aubio), but
    meaningfully harder to fool than a single-shot autocorrelation + chroma
    pass.
*/
class AudioAnalyzer
{
public:
    AnalysisResult analyze (const juce::AudioBuffer<float>& buffer, double sampleRate);

private:
    std::vector<float> toMono (const juce::AudioBuffer<float>& buffer);

    // --- BPM ---
    std::vector<float> computeOnsetEnvelope (const std::vector<float>& mono);

    struct TempoEstimate { double bpm = 0.0; float score = 0.0f; };
    TempoEstimate estimateTempoInRange (const std::vector<float>& onsetEnvelope, double frameRate,
                                         int startFrame, int endFrame);
    std::pair<double, float> estimateBpmRobust (const std::vector<float>& onsetEnvelope, double frameRate);

    // --- Key ---
    std::pair<juce::String, float> estimateKey (const std::vector<float>& mono, double sampleRate);
    std::array<float, 12> computeChromaVector (const std::vector<float>& mono, double sampleRate);

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
