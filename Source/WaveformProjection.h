#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <vector>

struct WaveformProjectionGroup {
    static constexpr std::size_t amplitudeBinCount = 65;
    std::uint64_t sampleStart = 0, sampleEnd = 0, sampleCount = 0;
    std::array<float, 2> minimum{}, maximum{};
    std::array<std::uint64_t, 2> minimumSample{}, maximumSample{};
    std::array<double, 2> sumSquares{}, rms{};
    std::array<std::array<double, 3>, 2> bandEnergy{}, bandPower{};
    // Sample counts, not normalized textures: preserve the actual distribution
    // under every zoom/reduction level, including a real partial final bucket.
    std::array<std::array<std::uint64_t, amplitudeBinCount>, 2> amplitudeCounts{};
};

struct WaveformProjectionInfo {
    double viewStart = 0, viewEnd = 0, samplesPerPixel = 0;
    std::uint64_t gridSamples = 1;
};

namespace waveformProjectionDetail {
inline double finiteSum(double a, double b) noexcept {
    const double limit = std::numeric_limits<double>::max();
    return b > limit - a ? limit : a + b;
}

template <class Column>
bool valid(const Column& column, std::uint64_t start) noexcept {
    for (std::size_t channel = 0; channel < 2; ++channel) {
        if (!std::isfinite(column.minimum[channel]) || !std::isfinite(column.maximum[channel])
            || column.minimum[channel] > column.maximum[channel]
            || column.minimumSample[channel] < start || column.minimumSample[channel] >= column.endSample
            || column.maximumSample[channel] < start || column.maximumSample[channel] >= column.endSample
            || !std::isfinite(column.sumSquares[channel]) || column.sumSquares[channel] < 0)
            return false;
        for (double energy : column.bandEnergy[channel])
            if (!std::isfinite(energy) || energy < 0)
                return false;
        // Valid source histograms account for exactly the observed samples.
        // Subtracting from the interval count also avoids overflow if a corrupt
        // external summary contains an arbitrarily large unsigned bin value.
        auto remaining = static_cast<std::uint64_t>(column.sampleCount);
        for (auto count : column.amplitudeCounts[channel]) {
            if (count > remaining) return false;
            remaining -= count;
        }
        if (remaining != 0) return false;
    }
    return true;
}

inline void finish(WaveformProjectionGroup& group) noexcept {
    for (std::size_t channel = 0; channel < 2; ++channel) {
        group.rms[channel] = std::sqrt(group.sumSquares[channel] / static_cast<double>(group.sampleCount));
        const auto& energy = group.bandEnergy[channel];
        const double scale = std::max({energy[0], energy[1], energy[2]});
        if (scale > 0) {
            // Scaling first keeps normalization finite even for huge finite
            // energies. Never weight previously normalized colour fractions.
            const double total = energy[0]/scale + energy[1]/scale + energy[2]/scale;
            for (std::size_t band = 0; band < 3; ++band)
                group.bandPower[channel][band] = (energy[band]/scale) / total;
        }
    }
}
}

// Projects ordered, sample-derived columns into stable audio-time groups. The
// container needs only size() and const operator[] (vector, deque, etc.). Its
// columns supply endSample, sampleCount, extrema/positions, sumSquares and raw
// bandEnergy and 65-bin amplitudeCounts. No JUCE/analyzer dependency is required.
//
// Grid boundaries are anchored at sample zero and rounded to whole analysis
// buckets using PHYSICAL pixel width. Advancing head does not re-bin retained
// columns. A group's true [sampleStart, sampleEnd) is never clipped or padded:
// the drawing code clips its footprint at the viewport edge. A partially filled
// supplied tail is valid, but no column whose end lies beyond head is exposed.
// Gaps split groups; overlapping, out-of-order or corrupt columns are ignored.
// Reuse output across paints to retain its allocation.
template <class Columns>
WaveformProjectionInfo projectWaveform(const Columns& columns, double headSample,
    double spanSamples, double physicalPixelWidth, std::uint64_t bucketSamples,
    std::vector<WaveformProjectionGroup>& output) {
    output.clear();
    WaveformProjectionInfo info;
    if (!std::isfinite(headSample) || headSample < 0 || !std::isfinite(spanSamples)
        || spanSamples <= 0 || !std::isfinite(physicalPixelWidth) || physicalPixelWidth <= 0)
        return info;
    info.viewStart = std::max(0.0, headSample - spanSamples);
    info.viewEnd = headSample;
    info.samplesPerPixel = spanSamples / physicalPixelWidth;
    if (!std::isfinite(info.samplesPerPixel))
        info.samplesPerPixel = std::numeric_limits<double>::max();
    bucketSamples = std::max<std::uint64_t>(1, bucketSamples);
    const auto maximumGrid = std::numeric_limits<std::uint64_t>::max();
    const double bucketsPerGroup = std::max(1.0,
        std::ceil(info.samplesPerPixel / static_cast<double>(bucketSamples)));
    info.gridSamples = bucketsPerGroup >= static_cast<double>(maximumGrid / bucketSamples)
        ? maximumGrid : static_cast<std::uint64_t>(bucketsPerGroup) * bucketSamples;

    std::uint64_t activeCell = 0, previousEnd = 0;
    bool haveGroup = false;
    for (std::size_t index = 0; index < columns.size(); ++index) {
        const auto& column = columns[index];
        if (column.sampleCount <= 0)
            continue;
        const auto count = static_cast<std::uint64_t>(column.sampleCount);
        if (column.endSample < count || static_cast<double>(column.endSample) > headSample
            || static_cast<double>(column.endSample) <= info.viewStart)
            continue;
        const auto start = column.endSample - count;
        if ((haveGroup && start < previousEnd) || !waveformProjectionDetail::valid(column, start))
            continue;
        const auto cell = start / info.gridSamples;
        if (!haveGroup || cell != activeCell || start != previousEnd) {
            if (haveGroup) waveformProjectionDetail::finish(output.back());
            output.emplace_back();
            auto& group = output.back();
            group.sampleStart = start;
            group.minimum = column.minimum;
            group.maximum = column.maximum;
            group.minimumSample = column.minimumSample;
            group.maximumSample = column.maximumSample;
            activeCell = cell;
            haveGroup = true;
        }
        auto& group = output.back();
        group.sampleEnd = column.endSample;
        group.sampleCount = group.sampleEnd - group.sampleStart;
        for (std::size_t channel = 0; channel < 2; ++channel) {
            if (column.minimum[channel] < group.minimum[channel]
                || (column.minimum[channel] == group.minimum[channel]
                    && column.minimumSample[channel] < group.minimumSample[channel])) {
                group.minimum[channel] = column.minimum[channel];
                group.minimumSample[channel] = column.minimumSample[channel];
            }
            if (column.maximum[channel] > group.maximum[channel]
                || (column.maximum[channel] == group.maximum[channel]
                    && column.maximumSample[channel] < group.maximumSample[channel])) {
                group.maximum[channel] = column.maximum[channel];
                group.maximumSample[channel] = column.maximumSample[channel];
            }
            group.sumSquares[channel] = waveformProjectionDetail::finiteSum(
                group.sumSquares[channel], column.sumSquares[channel]);
            for (std::size_t band = 0; band < 3; ++band)
                group.bandEnergy[channel][band] = waveformProjectionDetail::finiteSum(
                    group.bandEnergy[channel][band], column.bandEnergy[channel][band]);
            for (std::size_t bin = 0; bin < WaveformProjectionGroup::amplitudeBinCount; ++bin)
                group.amplitudeCounts[channel][bin] += column.amplitudeCounts[channel][bin];
        }
        previousEnd = column.endSample;
    }
    if (haveGroup) waveformProjectionDetail::finish(output.back());
    return info;
}
