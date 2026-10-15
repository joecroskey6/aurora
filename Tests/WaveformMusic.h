#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

// Original procedural QA music, not a recording or a licensed composition.
// Eight 4/4 bars at 80 BPM form a 24-second loop. Every sample is independently
// reproducible, so GUI block boundaries and callback timing cannot alter it.
namespace waveform_test_music {
struct StereoSample { float left = 0, right = 0; };

inline double noise(std::uint64_t index, std::uint64_t seed) noexcept {
    std::uint64_t value = index + seed + 0x9e3779b97f4a7c15ull;
    value = (value ^ (value >> 30)) * 0xbf58476d1ce4e5b9ull;
    value = (value ^ (value >> 27)) * 0x94d049bb133111ebull;
    value ^= value >> 31;
    return double(value >> 11) * (2.0 / 9007199254740992.0) - 1.0;
}

inline StereoSample sample(std::uint64_t absoluteSample, double sampleRate) noexcept {
    if (!std::isfinite(sampleRate) || sampleRate < 1000 || sampleRate > 768000) return {};
    constexpr double tau = 6.28318530717958647692;
    constexpr double beatSeconds = .75, stepSeconds = .1875;
    const auto loopSamples = static_cast<std::uint64_t>(std::llround(sampleRate * 24));
    const auto index = absoluteSample % loopSamples;
    const double t = double(index) / sampleRate;
    const int bar = std::min(7, int(t / 3));
    const double barTime = t - bar * 3;
    const int beat = int(barTime / beatSeconds), step = std::min(15, int(barTime / stepSeconds));
    const double beatTime = std::fmod(barTime, beatSeconds), stepTime = std::fmod(barTime, stepSeconds);
    // The breakdown contains genuine silence, not a cosmetic flat visual.
    if (t >= 18.5 && t < 19.25) return {};
    constexpr std::array<double, 8> roots { 65.4064, 58.2705, 51.9131, 58.2705,
                                          65.4064, 77.7817, 58.2705, 65.4064 };
    constexpr std::array<double, 8> drumGains { .85, 1, .9, 1, .3, 1, .16, 1 };
    constexpr std::array<double, 8> bassGains { .85, 1, .9, 1, .16, 1, .22, 1 };
    constexpr std::array<double, 8> padGains { .7, .8, 1, .6, 1, .75, .55, 1 };
    constexpr std::array<double, 8> arpGains { .65, .8, .55, 1, .5, 1, .15, .9 };
    const double root = roots[std::size_t(bar)];
    double l = 0, r = 0;

    const auto envelope = [](double age, double attack, double decay) {
        return (1.0 - std::exp(-age * attack)) * std::exp(-age * decay);
    };
    // A decaying pitch sweep, snare body/noise and alternating closed/open hats.
    const double kickPhase = tau * (48 * beatTime + 3.8 * (1 - std::exp(-beatTime * 28)));
    const double kick = .52 * envelope(beatTime, 1800, 12) * std::sin(kickPhase);
    const double snare = (beat == 1 || beat == 3)
        ? .24 * envelope(beatTime, 2200, 23)
            * (.55 * noise(index, 59) + .45 * std::sin(tau * 179 * beatTime)) : 0;
    const double hatNoise = .5 * (noise(index, 123) - noise(index == 0 ? loopSamples - 1 : index - 1, 123));
    const bool openHat = step == 6 || step == 14;
    const double hat = (openHat ? .09 : .062) * envelope(stepTime, 3200, openHat ? 22 : 75) * hatNoise;
    const double percussion = drumGains[std::size_t(bar)] * (kick + snare);
    l += percussion + drumGains[std::size_t(bar)] * hat * (step % 2 ? .45 : 1);
    r += percussion + drumGains[std::size_t(bar)] * hat * (step % 2 ? 1 : .45);

    constexpr std::array<int, 16> bassPattern { 0, -1, -1, 0, -1, -1, 7, -1, 0, -1, 12, -1, -1, -1, 7, -1 };
    const int bassNote = bassPattern[std::size_t(step)];
    if (bassNote >= 0) {
        const double f = root * std::pow(2., double(bassNote) / 12);
        const double phase = tau * f * stepTime;
        const double bass = .24 * bassGains[std::size_t(bar)] * envelope(stepTime, 650, 9)
            * (std::sin(phase) + .23 * std::sin(2 * phase) + .09 * std::sin(3 * phase));
        l += bass; r += bass * .94;
    }

    constexpr std::array<double, 4> chordRatios { 1, 1.189207115, 1.498307077, 1.781797436 };
    const double padEnvelope = (1 - std::exp(-barTime * 8)) * std::min(1., (3 - barTime) * 6);
    for (std::size_t voice = 0; voice < chordRatios.size(); ++voice) {
        const double f = root * 4 * chordRatios[voice];
        const double pan = double(voice) / 3;
        const double modulation = .13 * std::sin(tau * (.31 + .05 * voice) * t);
        const double pad = .038 * padGains[std::size_t(bar)] * padEnvelope
            * (std::sin(tau * f * barTime + modulation) + .32 * std::sin(tau * f * 2.001 * barTime));
        l += pad * (1 - pan * .72);
        r += .038 * padGains[std::size_t(bar)] * padEnvelope * (.28 + pan * .72)
            * (std::sin(tau * f * 1.0027 * barTime - modulation) + .32 * std::sin(tau * f * 1.998 * barTime));
    }

    constexpr std::array<int, 16> melody { 0, 7, 12, 15, 19, 12, 7, 10, 0, 12, 19, 22, 15, 12, 7, 3 };
    const double arpFrequency = root * 4 * std::pow(2., double(melody[std::size_t(step)]) / 12);
    const double arp = .112 * arpGains[std::size_t(bar)] * envelope(stepTime, 1100, 19)
        * (std::sin(tau * arpFrequency * stepTime) + .18 * std::sin(tau * arpFrequency * 3 * stepTime));
    const double pan = .5 + .38 * std::sin(step * 1.83 + bar * .7);
    l += arp * (1 - pan * .8); r += arp * (.2 + pan * .8);

    // Smooth bounded synthesis, with no sample clipping or isolated impulses.
    // This saturation is part of the source sound, never the waveform renderer.
    double fade = std::min(1., (24 - t) * 8);
    if (t >= 18.42 && t < 18.5) fade *= (18.5 - t) / .08;
    if (t >= 19.25 && t < 19.33) fade *= (t - 19.25) / .08;
    return { float(.92 * std::tanh(l * 1.5) * fade), float(.92 * std::tanh(r * 1.5) * fade) };
}
} // namespace waveform_test_music
