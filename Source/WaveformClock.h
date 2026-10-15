#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

// GUI-thread display clock for a sample-indexed waveform history. Audio sample
// counts are authoritative; wall time only interpolates between worker updates.
// The cursor is fractional, so a renderer can scroll across bucket boundaries
// smoothly without moving or resampling the history itself.
class WaveformClock final {
public:
    static constexpr double displayLagSeconds = 0.080;
    static constexpr double maximumExtrapolationSeconds = 0.160;

    void reset() noexcept { *this = WaveformClock{}; }

    // totalSamples may include a partially filled history bucket.
    // lastCompleteSample is the exclusive end of available measured history,
    // including a provisional tail when supplied by the analyzer.
    // nowSeconds must use a monotonic GUI clock. Frozen calls hold exactly; the
    // first unfrozen call reanchors to current audio, with the normal 80 ms lag.
    double update(std::uint64_t totalSamples, std::uint64_t lastCompleteSample,
                  double sampleRate, std::uint64_t generation,
                  double nowSeconds, bool frozen) noexcept {
        if (!std::isfinite(nowSeconds))
            return cursor;
        if (initialised && frozen) {
            wasFrozen = true;
            return cursor;
        }
        if (!std::isfinite(sampleRate) || sampleRate <= 0.0) {
            reset();
            return 0.0;
        }

        const auto completed = std::min(totalSamples, lastCompleteSample);
        const bool discontinuity = !initialised || wasFrozen
            || generation != streamGeneration || sampleRate != rate
            || totalSamples < previousTotal || completed < previousComplete
            || nowSeconds < previousTime;
        if (discontinuity) {
            anchor(totalSamples, completed, sampleRate, generation, nowSeconds);
            wasFrozen = frozen;
            return cursor;
        }

        const double elapsed = nowSeconds - previousTime;
        const double ageBeforeUpdate = nowSeconds - arrivalTime;
        const bool fresh = totalSamples > previousTotal;
        if (fresh) {
            // A stopped stream has consumed its real buffered tail. Refill the
            // display delay on resume without ever rewinding existing history.
            if (stalled || ageBeforeUpdate > maximumExtrapolationSeconds)
                buffering = true;
            arrivalSamples = static_cast<double>(totalSamples);
            arrivalTime = nowSeconds;
            stalled = false;
        }
        previousTotal = totalSamples;
        previousComplete = completed;
        previousTime = nowSeconds;
        const double end = static_cast<double>(completed);
        const double target = std::max(0.0, arrivalSamples - rate * displayLagSeconds);

        if (buffering) {
            if (target <= cursor)
                return cursor;
            cursor = std::max(cursor, std::min(end, target));
            buffering = false;
            return cursor;
        }

        const double age = std::max(0.0, nowSeconds - arrivalTime);
        if (age >= maximumExtrapolationSeconds) {
            // This tail is already present in history. No new samples, looped
            // waveform, or blank scrolling region are invented after it.
            cursor = end;
            stalled = true;
            return cursor;
        }
        if (elapsed <= 0.0)
            return cursor;

        // The nominal advance is independent of block size. Correct delivery
        // phase gently (350 ms time constant; at most +/-12% clock speed) so a
        // burst of worker snapshots cannot jerk the trace by a complete block.
        const double nominalAdvance = elapsed * rate;
        if (!std::isfinite(nominalAdvance)) {
            cursor = end;
            return cursor;
        }
        const double predicted = cursor + nominalAdvance;
        const double phaseTarget = target + std::min(age, displayLagSeconds) * rate;
        const double correction = std::clamp(
            (phaseTarget - predicted) * -std::expm1(-elapsed / 0.350),
            -nominalAdvance * 0.12, nominalAdvance * 0.12);
        cursor = std::clamp(predicted + correction, cursor, end);
        return cursor;
    }

private:
    void anchor(std::uint64_t total, std::uint64_t completed, double sampleRate,
                std::uint64_t generation, double now) noexcept {
        rate = sampleRate;
        streamGeneration = generation;
        previousTotal = total;
        previousComplete = completed;
        previousTime = arrivalTime = now;
        arrivalSamples = static_cast<double>(total);
        cursor = std::clamp(arrivalSamples - rate * displayLagSeconds,
                            0.0, static_cast<double>(completed));
        buffering = cursor == 0.0;
        stalled = false;
        wasFrozen = false;
        initialised = true;
    }

    double cursor = 0.0, rate = 0.0, previousTime = 0.0;
    double arrivalTime = 0.0, arrivalSamples = 0.0;
    std::uint64_t previousTotal = 0, previousComplete = 0, streamGeneration = 0;
    bool initialised = false, buffering = true, stalled = false, wasFrozen = false;
};
