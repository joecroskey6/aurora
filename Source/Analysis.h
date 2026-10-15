#pragma once

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

// Original, dependency-free stereo metering core. See docs/METERING.md for
// measurement definitions, calibration, and the integrated-loudness estimate.
struct AnalysisFrame
{
    std::vector<float> left, right; // Most recent 1024 waveform samples.
    std::vector<float> spectrum;    // 2049 bins: DC through Nyquist, in dBFS.
    double sampleRate = 48000.0;
    int channelCount = 2;           // Mono is duplicated visually, counted once in LUFS.
    float peakL = -120.0f, peakR = -120.0f;
    float peakHoldL = -120.0f, peakHoldR = -120.0f; // Every sample since reset.
    float rmsL = -120.0f, rmsR = -120.0f;
    float correlation = 0.0f;
    float momentaryLUFS = -120.0f;
    float shortTermLUFS = -120.0f;
    float integratedLUFS = -120.0f;
    float low = 0.0f, mid = 0.0f, high = 0.0f;
    std::uint64_t sequence = 0; // Advances on each new FFT and reset.
    std::uint64_t resetGeneration = 0; // Advances only on reset or format change.
};

// Continuous sample-derived history, separate from the small live FFT snapshot.
// Each completed column covers [endSample - sampleCount, endSample), with no
// gaps at process() boundaries. Its signed extrema include every finite input
// sample, and rms is the unweighted channel RMS over precisely that interval.
struct WaveformColumn
{
    static constexpr std::size_t amplitudeBinCount = 65;
    std::uint64_t sequence = 0; // Monotonic across resets; use as a delta cursor.
    std::uint64_t endSample = 0; // Exclusive sample clock within this generation.
    int sampleCount = 0;
    std::array<float, 2> minimum {}, maximum {}, rms {};
    // Exact positions of the first occurrence of each extremum, measured from
    // the start of this generation. Useful when drawing short transient peaks
    // without spreading them over an entire display aggregation interval.
    std::array<std::uint64_t, 2> minimumSample {}, maximumSample {};
    // Additive sufficient statistics. Aggregate these directly across columns;
    // reconstructing energy from float RMS or normalized band fractions loses
    // information (particularly during filter startup and decaying transients).
    std::array<double, 2> sumSquares {};
    std::array<std::array<double, 3>, 2> bandEnergy {};
    // Actual PCM amplitude occupancy. Bin centres are -1, -31/32, ..., 0,
    // ..., +1; each sanitized input sample increments exactly one nearest bin.
    // Values outside full scale saturate the end bins, while extrema and RMS
    // above remain unclipped. A 1 ms bucket holds at most 384 samples, so these
    // compact counters cannot overflow at any supported sample rate.
    std::array<std::array<std::uint16_t, amplitudeBinCount>, 2> amplitudeCounts {};
    // Fractions of filtered low / mid / high energy, independently per channel.
    // Sum is one, or zero when filter energy is zero. For colour, not a calibrated
    // band meter: 12 dB/oct Butterworth splits at 250 Hz and min(4 kHz, .45 Fs),
    // with the mid band formed by cascaded high/low passes. Low includes DC.
    std::array<std::array<float, 3>, 2> bandPower {};
};

struct WaveformHistorySnapshot
{
    std::vector<WaveformColumn> columns; // Oldest first, completed columns only.
    // Current incomplete bucket, if any. Replace it on every snapshot; it is
    // never part of the completed-column delta cursor (sequence is always 0).
    // Publishing the real partial interval also preserves the final samples
    // when an input stream stops before the next bucket boundary.
    std::optional<WaveformColumn> tail;
    std::uint64_t generation = 0;
    std::uint64_t totalSamples = 0; // Includes samples in the incomplete column.
    double sampleRate = 48000.0;
    int bucketSamples = 48; // round(Fs / 1000), so time never depends on UI rate.
    int channelCount = 2;
};

class AudioAnalyzer
{
public:
    static constexpr int fftSize = 4096;
    static constexpr int fftHop = 1024;
    static constexpr float silenceDB = -120.0f;

    AudioAnalyzer();
    ~AudioAnalyzer();
    AudioAnalyzer(const AudioAnalyzer&) = delete;
    AudioAnalyzer& operator=(const AudioAnalyzer&) = delete;

    // Worker-thread API, deliberately not an audio-callback API: these methods
    // synchronize with snapshot(). A null right channel duplicates the left.
    // channelCount is 1 or 2; mono excludes the duplicated R from loudness.
    // Invalid formats and empty input are ignored; NaN/Inf samples become 0.
    void process(const float* left, const float* right, int count, double sampleRate,
                 int channelCount = 2);
    void reset();
    AnalysisFrame snapshot() const;
    // UI/worker API only. Copies retained columns newer than afterSequence; zero
    // returns the full retained history. Storage covers at least 24.25 seconds.
    // If a reader falls behind the bounded ring, its next delta starts at the
    // oldest retained column. Reset/format changes clear samples and advance
    // generation, while column sequence remains monotonic for existing cursors.
    WaveformHistorySnapshot waveformSnapshot(std::uint64_t afterSequence = 0) const;

private:
    struct State;
    std::unique_ptr<State> state;
};
