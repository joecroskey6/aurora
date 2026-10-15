#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

// Projects an evenly spaced, DC-through-Nyquist dB spectrum onto one logarithmic
// display interval. The return value is the maximum of the piecewise-linear dB
// curve inside that interval, not its band energy or an estimated sinusoid level.
// This preserves narrow peaks when reducing resolution without importing bin
// centres outside a cell, which would make oversampled bass regions too broad.
//
// Centre and span are normalized to log(max(1 Hz, minimumHz)) through
// log(min(maximumHz, Nyquist)); default bounds are 20 Hz and 20 kHz. Centre
// is clamped to [0,1], a nonpositive span is a point sample, and edges are clipped.
// Invalid input returns -120 dB; a one-value spectrum is treated as constant.
// Finite levels at or above 0 dB are valid and are never clipped at the top.
inline float spectralPeak(const std::vector<float>& db, double sampleRate,
                          float normalizedCenter, float normalizedSpan,
                          float minimumHz = 20.f, float maximumHz = 20000.f) noexcept
{
    constexpr float floorDB = -120.0f;
    if (db.empty() || !std::isfinite(sampleRate) || sampleRate <= 0.0
        || !std::isfinite(normalizedCenter) || !std::isfinite(normalizedSpan)
        || !std::isfinite(minimumHz) || !std::isfinite(maximumHz))
        return floorDB;

    const double nyquist = sampleRate * 0.5;
    const double lower = std::max(1.0, double(minimumHz));
    const double upper = std::min(double(maximumHz), nyquist);
    if (!std::isfinite(lower) || !std::isfinite(upper) || upper <= lower) return floorDB;

    const auto valueAt = [&](std::size_t index) {
        return std::isfinite(db[index]) ? std::max(floorDB, db[index]) : floorDB;
    };
    if (db.size() == 1) return valueAt(0);

    const double centre = std::clamp(static_cast<double>(normalizedCenter), 0.0, 1.0);
    const double halfSpan = std::max(0.0, static_cast<double>(normalizedSpan)) * 0.5;
    const double lastBin = static_cast<double>(db.size() - 1);
    const double scale = lastBin / nyquist;
    const auto positionAt = [&](double x) {
        const double frequency = lower * std::pow(upper / lower, std::clamp(x, 0.0, 1.0));
        return std::clamp(frequency * scale, 0.0, lastBin);
    };
    const double low = positionAt(centre - halfSpan);
    const double high = positionAt(centre + halfSpan);

    const auto interpolate = [&](double position) {
        const auto first = static_cast<std::size_t>(std::floor(position));
        const auto second = std::min(first + 1, db.size() - 1);
        const double fraction = position - static_cast<double>(first);
        return static_cast<float>((1.0 - fraction) * valueAt(first) + fraction * valueAt(second));
    };
    float maximum = std::max(interpolate(low), interpolate(high));

    const auto first = static_cast<std::size_t>(std::ceil(low));
    const auto last = static_cast<std::size_t>(std::floor(high));
    if (first <= last)
        for (auto bin = first; bin <= last; ++bin)
            maximum = std::max(maximum, valueAt(bin));
    return maximum;
}
