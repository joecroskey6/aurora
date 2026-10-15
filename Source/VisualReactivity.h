#pragma once

#include "Analysis.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Artistic controls, not additional audio meters or a beat/tempo detector.
// All five values are finite and bounded to [0,1]. A sustained single-band tone
// settles near .62 in that band; a substantial attack can reach 1 and onset=1.
struct VisualReaction {
    float low = 0, mid = 0, high = 0, onset = 0, activity = 0;
};

// Single GUI-thread owner; no allocations and no audio callback work.
// Call update(frame, elapsedSeconds), typically 30 times/second. Freeze by not
// calling update() and drawing snapshot(). Reset is explicit and also follows
// AnalysisFrame.resetGeneration, sample-rate changes and channel-count changes.
//
// The analyzer's band controls encode (band RMS dBFS + 60)/60. Recovering linear
// RMS before normalization makes the response nearly invariant to input volume
// above the quiet gate; this cannot reconstruct energy already clipped at -60dB.
// An adaptive reference follows rises in .6s and falls in 2s, making departures
// from recent energy visible. Output attack/release times are 18/160ms; activity
// uses 15/100ms. Onsets are positive changes in band RMS, observed once per FFT
// sequence, with 180ms decay. They are musical transients, not asserted beats.
// Below -58dBFS total reconstructed band RMS the quiet gate is closed, reaching
// full response at -46dBFS. With no new FFT for 250ms, controls release to zero.
// After .8s without audible input, the next signal seeds fresh level references.
// GUI sampling can miss transients between snapshots; exact metering is untouched.
class VisualReactivity final {
public:
    VisualReaction update(const AnalysisFrame& frame, double dtSeconds) {
        const double dt = std::clamp(finite(dtSeconds, 0.0), 0.0, 0.25);
        if (dt <= 0.0) return output;
        const double rate = finite(frame.sampleRate, 48000.0);
        if (haveFrame && (frame.resetGeneration != generation || rate != sampleRate
                         || frame.channelCount != channelCount || frame.sequence < sequence))
            reset();

        const bool fresh = !haveFrame || frame.sequence != sequence;
        age += dt;
        float event = 0;
        if (fresh) {
            const std::array<float, 3> bands {frame.low, frame.mid, frame.high};
            std::array<double, 3> next{};
            for (std::size_t i = 0; i < 3; ++i) {
                const double encoded = std::clamp(finite(bands[i], 0.0), 0.0, 1.0);
                next[i] = encoded > 0.0 ? std::pow(10.0, encoded * 3.0 - 3.0) : 0.0;
            }
            const double nextTotal = length(next);
            const double nextGate = quietGate(nextTotal);
            if (nextGate > 0) {
                if (!haveSignal) {
                    // Seed references at the incoming level: quiet and loud
                    // versions of the same recording start with the same shape.
                    reference = next;
                    globalReference = nextTotal;
                    haveSignal = true;
                }
                double flux = 0;
                for (std::size_t i = 0; i < 3; ++i) {
                    const double floor = std::max(globalReference * 0.025, 1.0e-12);
                    const double riseDB = 20.0 * std::log10(std::max(next[i], floor)
                                                          / std::max(amplitude[i], floor));
                    // A 10dB jump has full strength; <1dB fluctuations do not
                    // create an event. Spectral weighting suppresses tiny bands.
                    const double rise = smoothStep(1.0, 10.0, riseDB);
                    const double weight = std::sqrt(next[i] / std::max(nextTotal, 1.0e-12));
                    flux = std::max(flux, rise * weight);
                }
                event = float(std::clamp(flux * nextGate, 0.0, 1.0));
            }
            amplitude = next;
            total = nextTotal;
            gate = nextGate;
            age = 0;
            sequence = frame.sequence;
            generation = frame.resetGeneration;
            sampleRate = rate;
            channelCount = frame.channelCount;
            haveFrame = true;
        }

        const bool receiving = age <= 0.25;
        const double audibleGate = receiving ? gate : 0.0;
        quietSeconds = audibleGate == 0.0 ? quietSeconds + dt : 0.0;
        if (quietSeconds >= 0.8) haveSignal = false;
        const double activityTarget = audibleGate;
        output.activity = float(follow(output.activity, activityTarget, dt, 0.015, 0.100));
        output.onset = std::max(event, float(output.onset * std::exp(-dt / 0.180)));
        if (audibleGate == 0.0 && output.onset < 0.00001f) output.onset = 0;

        std::array<float*, 3> outputs {&output.low, &output.mid, &output.high};
        for (std::size_t i = 0; i < 3; ++i) {
            const double baseline = std::max({reference[i], globalReference * 0.04, 1.0e-12});
            const double relative = std::clamp(amplitude[i] / baseline, 0.0, 4.0);
            const double balance = std::sqrt(amplitude[i] / std::max(total, 1.0e-12));
            const double target = audibleGate * std::clamp(
                0.62 * std::pow(relative, 0.75) * (0.30 + 0.70 * balance), 0.0, 1.0);
            *outputs[i] = float(follow(*outputs[i], target, dt, 0.018, 0.160));
            if (*outputs[i] < 0.00001f) *outputs[i] = 0;
            // Do not adapt the reference to silence or missing data. This stops
            // background noise gaining strength while the audio stream is idle.
            if (receiving && gate > 0)
                reference[i] = follow(reference[i], amplitude[i], dt, 0.60, 2.0);
        }
        if (receiving && gate > 0)
            globalReference = follow(globalReference, total, dt, 0.35, 2.5);
        if (output.activity < 0.00001f) output.activity = 0;
        return output;
    }

    void reset() noexcept { *this = VisualReactivity{}; }
    VisualReaction snapshot() const noexcept { return output; }

private:
    static double finite(double value, double fallback) noexcept {
        return std::isfinite(value) ? value : fallback;
    }
    static double smoothStep(double low, double high, double value) noexcept {
        const double t = std::clamp((value - low) / (high - low), 0.0, 1.0);
        return t * t * (3.0 - 2.0 * t);
    }
    static double quietGate(double rms) noexcept {
        return smoothStep(-58.0, -46.0, 20.0 * std::log10(std::max(rms, 1.0e-12)));
    }
    static double follow(double previous, double target, double dt,
                         double attack, double release) noexcept {
        const double tau = target > previous ? attack : release;
        return target + (previous - target) * std::exp(-dt / tau);
    }
    static double length(const std::array<double, 3>& values) noexcept {
        return std::sqrt(values[0] * values[0] + values[1] * values[1] + values[2] * values[2]);
    }

    VisualReaction output;
    std::array<double, 3> amplitude{}, reference{};
    double total = 0, gate = 0, globalReference = 0, age = 0, quietSeconds = 0, sampleRate = 48000;
    std::uint64_t sequence = 0, generation = 0;
    int channelCount = 2;
    bool haveFrame = false, haveSignal = false;
};
