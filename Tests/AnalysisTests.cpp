#include "../Source/Analysis.h"
#include "../Source/SpectralProjection.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>
#include <thread>
#include <vector>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;

void require(bool passed, const std::string& description)
{
    if (!passed)
    {
        std::cerr << "FAIL: " << description << '\n';
        std::exit(1);
    }
}

void near(double actual, double expected, double tolerance, const std::string& description)
{
    require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
            description + " (got " + std::to_string(actual) + ", expected " + std::to_string(expected) + ")");
}

std::vector<float> sine(double rate, int count, double frequency, double amplitude)
{
    std::vector<float> result(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
        result[static_cast<std::size_t>(i)] = static_cast<float>(amplitude * std::sin(2.0 * pi * frequency * i / rate));
    return result;
}

void feed(AudioAnalyzer& analyzer, const std::vector<float>& left, const std::vector<float>& right,
          double rate, int block = 512, int channelCount = 2)
{
    require(left.size() == right.size(), "test fixture has equal channels");
    for (std::size_t offset = 0; offset < left.size(); offset += block)
    {
        const auto count = static_cast<int>(std::min<std::size_t>(block, left.size() - offset));
        analyzer.process(left.data() + offset, right.data() + offset, count, rate, channelCount);
    }
}

void checkFinite(const AnalysisFrame& frame)
{
    for (float value : {frame.peakL, frame.peakR, frame.peakHoldL, frame.peakHoldR, frame.rmsL, frame.rmsR, frame.correlation,
                       frame.momentaryLUFS, frame.shortTermLUFS, frame.integratedLUFS,
                       frame.low, frame.mid, frame.high})
        require(std::isfinite(value), "all scalar meters are finite");
    for (float value : frame.spectrum) require(std::isfinite(value), "spectrum is finite");
    for (float value : frame.left) require(std::isfinite(value), "waveform is finite");
    require(frame.left.size() <= 1024 && frame.right.size() <= 1024, "waveform memory is bounded");
    require(frame.spectrum.size() == 2049, "FFT includes DC and Nyquist");
    require(frame.low >= 0 && frame.low <= 1 && frame.mid >= 0 && frame.mid <= 1 &&
            frame.high >= 0 && frame.high <= 1, "visual energies are bounded");
}

void checkWaveformFinite(const WaveformHistorySnapshot& history);

void silenceAndInvalidInput()
{
    AudioAnalyzer analyzer;
    std::vector<float> silence(48000 * 4, 0.0f);
    feed(analyzer, silence, silence, 48000);
    auto frame = analyzer.snapshot();
    checkFinite(frame);
    near(frame.peakL, -120, 0, "silence peak floor");
    near(frame.rmsL, -120, 0, "silence RMS floor");
    near(frame.momentaryLUFS, -120, 0, "silence momentary floor");
    near(frame.shortTermLUFS, -120, 0, "silence short-term floor");
    near(frame.integratedLUFS, -120, 0, "silence excluded by integrated gate");
    near(frame.correlation, 0, 0, "silent correlation has no NaN");
    near(frame.low + frame.mid + frame.high, 0, 0, "silence has no visual energy");

    const auto sequence = frame.sequence;
    analyzer.process(nullptr, nullptr, 0, 48000);
    analyzer.process(silence.data(), silence.data(), 100, std::numeric_limits<double>::quiet_NaN());
    analyzer.process(silence.data(), silence.data(), 100, 0);
    require(analyzer.snapshot().sequence == sequence, "invalid input is a no-op");
    std::vector<float> invalid(8192, std::numeric_limits<float>::infinity());
    invalid[0] = std::numeric_limits<float>::quiet_NaN();
    feed(analyzer, invalid, invalid, 48000);
    checkFinite(analyzer.snapshot());
    std::cout << "PASS silence, invalid input, and bounded output\n";
}

void spectrumAndPhase()
{
    constexpr int bin = 128;
    const auto tone = sine(48000, 48000, bin * 48000.0 / AudioAnalyzer::fftSize, 0.5);
    auto negative = tone;
    for (auto& sample : negative) sample *= -1;
    AudioAnalyzer same, opposite, oneChannel;
    feed(same, tone, tone, 48000);
    feed(opposite, tone, negative, 48000);
    feed(oneChannel, tone, std::vector<float>(tone.size(), 0.0f), 48000);
    const auto a = same.snapshot(), b = opposite.snapshot(), c = oneChannel.snapshot();
    near(a.spectrum[bin], -6.0206, 0.002, "Hann coherent gain preserves tone amplitude");
    near(c.spectrum[bin], -9.0309, 0.002, "stereo power averaging of one active channel");
    near(a.spectrum[bin], b.spectrum[bin], 0.0001, "anti-phase spectrum never cancels");
    near(a.correlation, 1, 0.00001, "in-phase correlation");
    near(b.correlation, -1, 0.00001, "anti-phase correlation");
    near(c.correlation, 0, 0, "single-channel correlation is undefined -> zero");
    near(a.peakL, -6.0206, 0.002, "sample peak calibration");
    near(a.rmsL, -9.0309, 0.002, "RMS sine calibration");
    near(a.integratedLUFS, b.integratedLUFS, 0.0001, "loudness does not cancel anti-phase audio");
    require(a.mid > a.low + 0.5f && a.mid > a.high + 0.5f, "mid-band tone drives mid visual energy");

    // Edge bins must not receive the one-sided interior-bin factor of two.
    AudioAnalyzer edge;
    std::vector<float> dc(4096, 0.5f), nyquist(4096, 0.5f);
    for (std::size_t i = 1; i < nyquist.size(); i += 2) nyquist[i] = -0.5f;
    feed(edge, dc, dc, 48000);
    near(edge.snapshot().spectrum[0], -6.0206, 0.002, "DC FFT normalization");
    edge.reset();
    feed(edge, nyquist, nyquist, 48000);
    near(edge.snapshot().spectrum[2048], -6.0206, 0.002, "Nyquist FFT normalization");
    std::cout << "PASS FFT amplitude, edge bins, RMS, and anti-phase stereo\n";
}

void loudnessCalibrationAndTimeWindows()
{
    for (double sampleRate : {44100.0, 48000.0, 96000.0, 192000.0})
    {
        AudioAnalyzer analyzer;
        const auto tone = sine(sampleRate, static_cast<int>(4 * sampleRate), 1000, std::pow(10.0, -18.0 / 20.0));
        feed(analyzer, tone, tone, sampleRate, 1001);
        const auto frame = analyzer.snapshot();
        near(frame.momentaryLUFS, -18.0, 0.1, "1 kHz -18 dBFS stereo momentary at " + std::to_string(sampleRate));
        near(frame.shortTermLUFS, -18.0, 0.1, "1 kHz -18 dBFS stereo short-term");
        near(frame.integratedLUFS, -18.0, 0.1, "1 kHz -18 dBFS stereo integrated");
        near(frame.sampleRate, sampleRate, 0, "sample rate is reported");
    }

    AudioAnalyzer analyzer;
    const auto tiny = sine(48000, 4800, 1000, 0.1);
    feed(analyzer, tiny, tiny, 48000);
    near(analyzer.snapshot().momentaryLUFS, -120, 0, "momentary waits for full 400 ms");
    for (int i = 0; i < 3; ++i) feed(analyzer, tiny, tiny, 48000);
    near(analyzer.snapshot().momentaryLUFS, -20, 0.1, "momentary arrives at 400 ms");
    near(analyzer.snapshot().shortTermLUFS, -120, 0, "short-term waits for full 3 s");
    for (int i = 4; i < 30; ++i) feed(analyzer, tiny, tiny, 48000);
    near(analyzer.snapshot().shortTermLUFS, -20, 0.1, "short-term arrives at 3 s");
    std::vector<float> silence(4800, 0);
    for (int i = 0; i < 3; ++i) feed(analyzer, silence, silence, 48000);
    require(analyzer.snapshot().momentaryLUFS > -30, "400 ms window retains last tone segment");
    // Filter tail remains in the first silent block; one further block drains it.
    for (int i = 0; i < 2; ++i) feed(analyzer, silence, silence, 48000);
    require(analyzer.snapshot().momentaryLUFS < -100, "momentary window releases after silence");
    require(analyzer.snapshot().shortTermLUFS > -25, "3 s window still retains preceding tone");
    std::cout << "PASS K weighting at four rates and 400 ms / 3 s timing\n";
}

void callbackIndependenceAndReset()
{
    auto tone = sine(48000, 48000 * 4, 997, 0.25);
    for (std::size_t i = 0; i < tone.size(); ++i)
        tone[i] *= static_cast<float>(0.5 + 0.5 * std::sin(2.0 * pi * 0.75 * i / 48000.0));
    AudioAnalyzer small, uneven, large;
    feed(small, tone, tone, 48000, 64);
    feed(uneven, tone, tone, 48000, 997);
    feed(large, tone, tone, 48000, static_cast<int>(tone.size()));
    const auto expected = small.snapshot();
    for (const auto& frame : {uneven.snapshot(), large.snapshot()})
    {
        near(frame.momentaryLUFS, expected.momentaryLUFS, 0, "momentary independent of callback size");
        near(frame.shortTermLUFS, expected.shortTermLUFS, 0, "short-term independent of callback size");
        near(frame.integratedLUFS, expected.integratedLUFS, 0, "integrated independent of callback size");
        near(frame.rmsL, expected.rmsL, 0, "RMS independent of callback size");
        require(frame.sequence == expected.sequence, "FFT advances by samples, not callbacks");
        require(frame.spectrum == expected.spectrum, "FFT independent of callback size");
    }
    uneven.reset();
    auto reset = uneven.snapshot();
    near(reset.integratedLUFS, -120, 0, "reset clears integrated history");
    near(reset.low + reset.mid + reset.high, 0, 0, "reset clears visual energies");
    require(reset.sequence > expected.sequence, "reset advances sequence");
    require(reset.resetGeneration > expected.resetGeneration, "reset advances history generation");
    const auto newTone = sine(44100, 4410, 1000, 0.1);
    feed(small, newTone, newTone, 44100);
    reset = small.snapshot();
    near(reset.sampleRate, 44100, 0, "sample rate changes propagate");
    near(reset.integratedLUFS, -120, 0, "sample rate change starts a fresh measurement");
    require(reset.resetGeneration > expected.resetGeneration, "sample rate change advances history generation");
    std::cout << "PASS callback independence, reset, and sample-rate changes\n";
}

void relativeAndAbsoluteGating()
{
    AudioAnalyzer analyzer;
    const auto loud = sine(48000, 48000 * 12, 1000, std::pow(10.0, -23.0 / 20.0));
    const auto quiet = sine(48000, 48000 * 4, 1000, std::pow(10.0, -43.0 / 20.0));
    feed(analyzer, quiet, quiet, 48000);
    feed(analyzer, loud, loud, 48000);
    feed(analyzer, quiet, quiet, 48000);
    near(analyzer.snapshot().integratedLUFS, -23, 0.2, "relative gate excludes quiet passage with transition tolerance");
    const auto before = analyzer.snapshot().integratedLUFS;
    const std::vector<float> silence(48000 * 4, 0);
    feed(analyzer, silence, silence, 48000);
    near(analyzer.snapshot().integratedLUFS, before, 0.0001, "absolute gate excludes silence");
    AudioAnalyzer belowGate;
    const auto veryQuiet = sine(48000, 48000, 1000, std::pow(10.0, -75.0 / 20.0));
    feed(belowGate, veryQuiet, veryQuiet, 48000);
    near(belowGate.snapshot().integratedLUFS, -120, 0, "sub -70 LUFS blocks excluded");
    require(belowGate.snapshot().momentaryLUFS > -80, "momentary remains ungated");
    std::cout << "PASS relative and absolute loudness gating\n";
}

void concurrentSnapshots()
{
    AudioAnalyzer analyzer;
    const auto tone = sine(48000, 1024, 1000, 0.1);
    std::atomic<bool> done {false};
    std::thread producer([&] {
        for (int i = 0; i < 300; ++i)
        {
            analyzer.process(tone.data(), nullptr, 1024, 48000);
            if (i % 37 == 0) analyzer.reset();
        }
        done = true;
    });
    std::uint64_t waveformCursor = 0;
    do
    {
        checkFinite(analyzer.snapshot());
        const auto history = analyzer.waveformSnapshot(waveformCursor);
        checkWaveformFinite(history);
        if (!history.columns.empty()) waveformCursor = history.columns.back().sequence;
    } while (!done.load());
    producer.join();
    std::cout << "PASS concurrent worker, reset, and UI snapshots\n";
}

void peakHoldsAndMonoLoudness()
{
    AudioAnalyzer analyzer;
    std::vector<float> impulse(48000, 0);
    impulse[7] = 1.0f;
    feed(analyzer, impulse, impulse, 48000);
    auto frame = analyzer.snapshot();
    near(frame.peakL, -120, 0, "current peak returns to silence after isolated impulse");
    near(frame.peakHoldL, 0, 0, "peak hold captures an impulse missed by UI polling");
    near(frame.peakHoldR, 0, 0, "right peak hold captures every sample");
    analyzer.reset();
    near(analyzer.snapshot().peakHoldL, -120, 0, "reset clears peak hold");
    const float firstSample = 0.5f;
    analyzer.process(&firstSample, nullptr, 1, 48000);
    near(analyzer.snapshot().peakHoldL, -6.0206, 0.002, "peak hold does not wait for a full meter window");

    const auto tone = sine(48000, 48000 * 4, 1000, std::pow(10.0, -18.0 / 20.0));
    analyzer.reset();
    feed(analyzer, tone, tone, 48000, 512, 2);
    const auto stereoFrame = analyzer.snapshot();
    const float stereoLoudness = stereoFrame.integratedLUFS;
    analyzer.process(tone.data(), tone.data(), 4800, 48000, 1);
    frame = analyzer.snapshot();
    near(frame.integratedLUFS, -120, 0, "channel count change resets loudness history");
    require(frame.channelCount == 1, "frame reports mono analysis");
    require(frame.resetGeneration > stereoFrame.resetGeneration, "channel count change advances history generation");
    const auto monoGeneration = frame.resetGeneration;
    feed(analyzer, tone, tone, 48000, 512, 1);
    frame = analyzer.snapshot();
    require(frame.resetGeneration == monoGeneration, "ordinary audio preserves history generation");
    near(frame.integratedLUFS, stereoLoudness - 3.0103, 0.01, "mono loudness counts its signal once");
    near(frame.momentaryLUFS, -21.0103, 0.1, "mono 1 kHz loudness calibration");
    near(frame.peakHoldL, -18.0, 0.002, "channel count does not change sample peak calibration");
    analyzer.reset();
    require(analyzer.snapshot().channelCount == 1, "explicit reset preserves channel count");
    std::cout << "PASS exact peak holds, mono loudness, and channel-count reset\n";
}

void spectralProjectionEdges()
{
    // At 160 Hz the five grid points are 0,20,40,60,80 Hz.
    const std::vector<float> ramp {-80, -60, -40, -20, 0};
    near(spectralPeak(ramp, 160, 0, 0), -60, 0.0001, "projection begins at exactly 20 Hz");
    near(spectralPeak(ramp, 160, 1, 0), 0, 0.0001, "projection ends exactly at Nyquist");
    near(spectralPeak(ramp, 160, 0.5f, 0), -40, 0.0001, "logarithmic midpoint has correct frequency");
    const float thirtyHz = static_cast<float>(std::log(30.0 / 20.0) / std::log(80.0 / 20.0));
    near(spectralPeak(ramp, 160, thirtyHz, 0), -50, 0.0001, "point samples interpolate in dB");
    near(spectralPeak(ramp, 160, thirtyHz, -1), -50, 0.0001, "negative spans become point samples");
    const float width = 0.001f;
    const double upperFrequency = 20.0 * std::pow(4.0, static_cast<double>(thirtyHz) + width * 0.5);
    near(spectralPeak(ramp, 160, thirtyHz, width), -60.0 + (upperFrequency - 20.0), 0.0001,
         "narrow intervals exclude a louder bin outside their frequency edges");
    near(spectralPeak(ramp, 160, -1, 0), -60, 0.0001, "low out-of-range centre is clamped");
    near(spectralPeak(ramp, 160, 2, 0), 0, 0.0001, "high out-of-range centre is clamped");
    near(spectralPeak(ramp, 160, 0.5f, 4), 0, 0.0001, "wide interval clips to visible range");

    std::vector<float> aboveCap(2049, -120);
    aboveCap[1707] = 0; // 20003.90625 Hz at 48 kHz: outside the visible domain.
    near(spectralPeak(aboveCap, 48000, 1, 0.005f), -40, 0.001,
         "20 kHz edge uses interpolation, not an out-of-range bin maximum");

    const std::vector<float> validPositive {6, 6, 6};
    near(spectralPeak(validPositive, 48000, 0.5f, 0.1f), 6, 0, "positive dBFS is valid");
    near(spectralPeak(std::vector<float> {0}, 48000, 0.5f, 0.1f), 0, 0, "single-bin zero dBFS is valid");
    near(spectralPeak({}, 48000, 0.5f, 0.1f), -120, 0, "empty spectrum returns silence");
    near(spectralPeak(ramp, 0, 0.5f, 0.1f), -120, 0, "zero rate returns silence");
    near(spectralPeak(ramp, 20, 0.5f, 0.1f), -120, 0, "no visible frequencies returns silence");
    near(spectralPeak(ramp, 48000, std::numeric_limits<float>::quiet_NaN(), 0.1f), -120, 0,
         "nonfinite position returns silence");
    near(spectralPeak(std::vector<float> {std::numeric_limits<float>::quiet_NaN()}, 48000, 0, 0), -120, 0,
         "nonfinite spectrum entry returns silence");
    std::cout << "PASS log-spectrum frequency edges, interpolation, and valid zero/positive dBFS\n";
}

// Reference in physical frequency coordinates: clip each piecewise-linear
// segment to the requested interval and evaluate its two intersection ends.
// This deliberately does not use the production bin-index bound/ceil/floor path.
float referenceFrequencyMaximum(const std::vector<float>& spectrum, double rate,
                                double lowHz, double highHz)
{
    float result = -120;
    const auto finite = [](float value) { return std::isfinite(value) ? std::max(-120.f, value) : -120.f; };
    for (std::size_t i = 1; i < spectrum.size(); ++i) {
        const double a = double(i - 1) * rate * .5 / double(spectrum.size() - 1);
        const double b = double(i) * rate * .5 / double(spectrum.size() - 1);
        if (a > highHz || b < lowHz) continue;
        for (const double frequency : { std::max(a, lowHz), std::min(b, highHz) }) {
            const double t = (frequency - a) / (b - a);
            result = std::max(result, float((1 - t) * finite(spectrum[i - 1]) + t * finite(spectrum[i])));
        }
    }
    return result;
}

void spectralProjectionCustomRanges()
{
    for (const double rate : {8000., 44100., 48000., 96000., 192000., 384000.}) {
        std::vector<float> spectrum(513);
        for (std::size_t i = 0; i < spectrum.size(); ++i)
            spectrum[i] = i % 17 == 0 ? 6.f : float(-70. + 33. * std::sin(double(i) * .27));
        spectrum[14] = std::numeric_limits<float>::quiet_NaN();
        for (const auto bounds : {std::pair<float, float>{10, 24000}, {500, 2000},
                                  {20, 20000}, {3000, 30000}, {1, 500}}) {
            const double low = bounds.first, high = std::min(double(bounds.second), rate * .5);
            for (int cell = 0; cell <= 128; ++cell) {
                const float center = float(cell) / 128.f;
                for (const float span : {0.f, 1.f / 128.f, .27f, 3.f}) {
                    const double left = std::clamp(double(center) - double(span) * .5, 0., 1.);
                    const double right = std::clamp(double(center) + double(span) * .5, 0., 1.);
                    const double beginHz = std::exp(std::log(low) + left * (std::log(high) - std::log(low)));
                    const double endHz = std::exp(std::log(low) + right * (std::log(high) - std::log(low)));
                    near(spectralPeak(spectrum, rate, center, span, bounds.first, bounds.second),
                         referenceFrequencyMaximum(spectrum, rate, beginHz, endHz), .0002,
                         "custom log bands match independent physical-frequency segment intersections");
                }
            }
        }
    }
    std::vector<float> edge(51, -120); // 10 Hz bin spacing at 1 kHz sample rate.
    edge[2] = 0; // 20 Hz lies just below the custom 23 Hz lower boundary.
    near(spectralPeak(edge, 1000, 0, 0, 23, 117), -36, .0001,
         "custom lower boundary interpolates an outside peak without importing its maximum");
    edge[2] = -120;
    edge[12] = 0; // 120 Hz lies just above the 117 Hz upper boundary.
    near(spectralPeak(edge, 1000, 1, .001f, 23, 117), -36, .0001,
         "custom upper boundary interpolates an outside peak without importing its maximum");
    edge[12] = -120;
    edge.back() = 3;
    near(spectralPeak(edge, 1000, 1, 0, 10, 24000), 3, 0,
         "custom maximum above Nyquist stops at the measured Nyquist bin");
    near(spectralPeak(edge, 1000, .5f, 1, 500, 24000), -120, 0,
         "custom minimum at Nyquist leaves no logarithmic display interval");
    near(spectralPeak(edge, 1000, .5f, 1, 600, 24000), -120, 0,
         "custom minimum above Nyquist leaves no visible interval");
    near(spectralPeak(edge, 1000, .5f, 1, 100, 100), -120, 0,
         "equal custom limits do not divide by a zero log span");
    near(spectralPeak(edge, 1000, .5f, 1, 200, 100), -120, 0,
         "reversed custom limits return silence");
    for (const float invalid : {std::numeric_limits<float>::quiet_NaN(),
                               std::numeric_limits<float>::infinity(),
                               -std::numeric_limits<float>::infinity()}) {
        near(spectralPeak(edge, 1000, .5f, 1, invalid, 400), -120, 0,
             "nonfinite custom lower frequency is rejected before clamping");
        near(spectralPeak(edge, 1000, .5f, 1, 10, invalid), -120, 0,
             "nonfinite custom upper frequency is rejected before Nyquist clipping");
    }
    near(spectralPeak(edge, 1000, .5f, .1f, -20, 200),
         spectralPeak(edge, 1000, .5f, .1f, 1, 200), 0,
         "finite custom lower limits clamp to one Hz for a defined log scale");
    std::cout << "PASS custom spectral ranges against frequency-domain oracle, Nyquist and outside-band peaks\n";
}

void spectralProjectionPreservesNarrowPeaks()
{
    for (const double sampleRate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0})
    {
        std::vector<float> spectrum(2049, -120);
        const double binWidth = sampleRate / 4096;
        const int firstVisible = static_cast<int>(std::ceil(20.0 / binWidth));
        const int lastVisible = static_cast<int>(std::floor(std::min(20000.0, sampleRate * 0.5) / binWidth));
        for (int bin = firstVisible; bin <= lastVisible; ++bin)
        {
            const float level = bin % 3 == 0 ? 6.0f : (bin % 3 == 1 ? 0.0f : -18.0f);
            spectrum[static_cast<std::size_t>(bin)] = level;
            float gpuMaximum = -120, graphMaximum = -120;
            // GPU texture: 128 contiguous cells with centres half a cell in.
            for (int cell = 0; cell < 128; ++cell)
                gpuMaximum = std::max(gpuMaximum, spectralPeak(spectrum, sampleRate,
                                      (static_cast<float>(cell) + 0.5f) / 128.0f, 1.0f / 128.0f));
            // Existing graph: 201 centres, including both domain boundaries.
            for (int point = 0; point <= 200; ++point)
                graphMaximum = std::max(graphMaximum, spectralPeak(spectrum, sampleRate,
                                        static_cast<float>(point) / 200.0f, 1.0f / 200.0f));
            near(gpuMaximum, level, 0.001, "GPU projection preserves every visible narrow-bin peak");
            near(graphMaximum, level, 0.001, "graph projection preserves every visible narrow-bin peak");
            spectrum[static_cast<std::size_t>(bin)] = -120;
        }
    }
    std::cout << "PASS every visible narrow-bin peak on 128-cell GPU and 200-cell graph grids at five rates\n";
}

void checkWaveformFinite(const WaveformHistorySnapshot& history)
{
    require(history.columns.size() <= 32768, "continuous waveform memory is bounded");
    std::uint64_t previousSequence = 0, previousEnd = 0;
    for (const auto& column : history.columns)
    {
        require(column.sequence > previousSequence, "waveform columns have increasing cursors");
        require(column.endSample <= history.totalSamples, "waveform does not precede its sample clock");
        require(column.sampleCount == history.bucketSamples, "published waveform buckets are complete");
        if (previousEnd > 0)
            require(column.endSample == previousEnd + static_cast<std::uint64_t>(column.sampleCount),
                    "waveform columns have no sample gaps");
        previousSequence = column.sequence;
        previousEnd = column.endSample;
        for (int channel = 0; channel < 2; ++channel)
        {
            require(std::isfinite(column.minimum[channel]) && std::isfinite(column.maximum[channel]) &&
                    std::isfinite(column.rms[channel]), "continuous waveform extrema and RMS are finite");
            require(column.minimum[channel] <= column.maximum[channel], "signed waveform extrema are ordered");
            require(column.rms[channel] >= 0, "continuous waveform RMS is nonnegative");
            std::uint64_t amplitudeSamples = 0;
            for (auto count : column.amplitudeCounts[channel]) amplitudeSamples += count;
            require(amplitudeSamples == static_cast<std::uint64_t>(column.sampleCount),
                    "completed amplitude distribution counts every actual PCM sample exactly once");
            require(std::isfinite(column.sumSquares[channel]) && column.sumSquares[channel] >= 0,
                    "raw waveform sum of squares remains finite");
            require(column.minimumSample[channel] >= column.endSample - column.sampleCount &&
                    column.minimumSample[channel] < column.endSample &&
                    column.maximumSample[channel] >= column.endSample - column.sampleCount &&
                    column.maximumSample[channel] < column.endSample,
                    "both extrema positions lie within their exact source bucket");
            float sum = 0;
            for (int band = 0; band < 3; ++band)
            {
                const auto weight = column.bandPower[channel][band];
                require(std::isfinite(weight) && weight >= 0 && weight <= 1, "waveform colour fractions are finite and bounded");
                require(std::isfinite(column.bandEnergy[channel][band]) && column.bandEnergy[channel][band] >= 0,
                        "raw filter energies remain finite and nonnegative");
                sum += weight;
            }
            require(sum == 0 || std::abs(sum - 1) < 0.000001f, "waveform colour is normalized independently per channel");
        }
    }
    if (history.tail)
    {
        const auto& tail = *history.tail;
        require(tail.sequence == 0 && tail.sampleCount > 0 && tail.sampleCount < history.bucketSamples &&
                tail.endSample == history.totalSamples, "provisional tail describes only the real incomplete interval");
        if (!history.columns.empty())
            require(tail.endSample - tail.sampleCount == history.columns.back().endSample,
                    "provisional tail starts exactly after the completed history");
        for (int channel = 0; channel < 2; ++channel)
        {
            require(std::isfinite(tail.minimum[channel]) && std::isfinite(tail.maximum[channel]) &&
                    std::isfinite(tail.rms[channel]) && std::isfinite(tail.sumSquares[channel]),
                    "partial bucket measurements are finite");
            require(tail.minimumSample[channel] >= tail.endSample - tail.sampleCount &&
                    tail.minimumSample[channel] < tail.endSample &&
                    tail.maximumSample[channel] >= tail.endSample - tail.sampleCount &&
                    tail.maximumSample[channel] < tail.endSample, "partial extrema timestamps remain within their interval");
            for (auto energy : tail.bandEnergy[channel])
                require(std::isfinite(energy) && energy >= 0, "partial filtered energies remain finite and nonnegative");
            std::uint64_t amplitudeSamples = 0;
            for (auto count : tail.amplitudeCounts[channel]) amplitudeSamples += count;
            require(amplitudeSamples == static_cast<std::uint64_t>(tail.sampleCount),
                    "partial amplitude distribution contains only its actual observed samples");
        }
    }
}

void continuousWaveformSamplesAndDeltas()
{
    auto left = sine(48000, 48000 + 17, 157, 0.35);
    auto right = sine(48000, static_cast<int>(left.size()), 6173, 0.22);
    left[7] = 1.0f;
    left[48] = -0.99f;
    right[47] = -0.875f;
    right[238] = 0.77f;
    AudioAnalyzer oneBlock, oddBlocks, frequentPolls;
    feed(oneBlock, left, right, 48000, static_cast<int>(left.size()));
    feed(oddBlocks, left, right, 48000, 997);
    const auto expected = oneBlock.waveformSnapshot();
    const auto uneven = oddBlocks.waveformSnapshot();
    checkWaveformFinite(expected);
    require(expected.columns.size() == 1000, "waveform publishes 1000 complete buckets per second at 48 kHz");
    require(expected.totalSamples == left.size(), "sample clock includes incomplete trailing bucket");
    require(expected.generation == oneBlock.snapshot().resetGeneration, "waveform and FFT generations match");
    require(expected.bucketSamples == 48, "waveform buckets use sample-based timing");

    std::vector<WaveformColumn> deltas;
    std::uint64_t cursor = 0;
    for (std::size_t offset = 0; offset < left.size(); offset += 83)
    {
        const int count = static_cast<int>(std::min<std::size_t>(83, left.size() - offset));
        frequentPolls.process(left.data() + offset, right.data() + offset, count, 48000);
        const auto delta = frequentPolls.waveformSnapshot(cursor);
        deltas.insert(deltas.end(), delta.columns.begin(), delta.columns.end());
        if (!delta.columns.empty()) cursor = delta.columns.back().sequence;
        require(frequentPolls.waveformSnapshot(cursor).columns.empty(), "polling unchanged history returns no repeated columns");
    }
    require(deltas.size() == expected.columns.size() && uneven.columns.size() == expected.columns.size(),
            "waveform length is independent of callback and polling cadence");
    for (std::size_t index = 0; index < expected.columns.size(); ++index)
    {
        const auto& column = expected.columns[index];
        for (const auto* comparison : std::array<const WaveformColumn*, 2> {&uneven.columns[index], &deltas[index]})
        {
            require(comparison->minimum == column.minimum && comparison->maximum == column.maximum &&
                    comparison->rms == column.rms && comparison->bandPower == column.bandPower &&
                    comparison->sumSquares == column.sumSquares && comparison->bandEnergy == column.bandEnergy &&
                    comparison->amplitudeCounts == column.amplitudeCounts &&
                    comparison->minimumSample == column.minimumSample && comparison->maximumSample == column.maximumSample &&
                    comparison->endSample == column.endSample && comparison->sequence == column.sequence,
                    "every waveform measurement is bit-identical across arbitrary chunks and polling");
        }
        const std::size_t start = static_cast<std::size_t>(column.endSample - column.sampleCount);
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto& samples = channel == 0 ? left : right;
            float smallest = samples[start], largest = samples[start];
            std::uint64_t smallestSample = start, largestSample = start;
            double power = 0;
            for (std::size_t sample = start; sample < column.endSample; ++sample)
            {
                if (samples[sample] < smallest) { smallest = samples[sample]; smallestSample = sample; }
                if (samples[sample] > largest) { largest = samples[sample]; largestSample = sample; }
                const double value = samples[sample];
                power += value * value;
            }
            near(column.minimum[channel], smallest, 0, "history retains the exact minimum of every sample bucket");
            near(column.maximum[channel], largest, 0, "history retains the exact maximum of every sample bucket");
            require(column.minimumSample[channel] == smallestSample && column.maximumSample[channel] == largestSample,
                    "history retains exact earliest extrema positions without UI-bucket spreading");
            near(column.sumSquares[channel], power, 0, "raw PCM sum of squares is preserved exactly before RMS rounding");
            near(column.rms[channel], std::sqrt(power / column.sampleCount), 0.00000006,
                 "waveform RMS measures the same exact PCM interval as its extrema");
        }
    }
    near(expected.columns[0].maximum[0], 1, 0, "left transient survives between UI polls");
    near(expected.columns[0].minimum[1], -0.875, 0, "right transient survives at bucket boundary");
    near(expected.columns[1].minimum[0], -0.99f, 0, "next-bucket transient is not lost at chunk boundary");
    std::cout << "PASS continuous stereo PCM extrema, RMS, complete sample coverage, and delta polling\n";
}

void continuousWaveformColourAndMono()
{
    for (int targetBand = 0; targetBand < 3; ++targetBand)
    {
        const double frequency = std::array<double, 3> {80, 1000, 10000}[targetBand];
        const auto tone = sine(48000, 48000, frequency, 0.5);
        AudioAnalyzer analyzer;
        analyzer.process(tone.data(), nullptr, static_cast<int>(tone.size()), 48000, 1);
        const auto history = analyzer.waveformSnapshot();
        checkWaveformFinite(history);
        require(history.channelCount == 1, "waveform reports mono format");
        double meanFraction = 0;
        int count = 0;
        for (const auto& column : history.columns)
        {
            require(column.minimum[0] == column.minimum[1] && column.maximum[0] == column.maximum[1] &&
                    column.rms[0] == column.rms[1] && column.bandPower[0] == column.bandPower[1],
                    "null right channel duplicates the mono waveform exactly");
            if (column.endSample > 9600)
            {
                meanFraction += column.bandPower[0][targetBand];
                ++count;
            }
        }
        require(meanFraction / count > 0.96, "waveform tone colour is dominated by the measured frequency band");
    }
    AudioAnalyzer stereo;
    const auto low = sine(48000, 48000, 80, 0.4);
    const auto high = sine(48000, 48000, 10000, 0.4);
    feed(stereo, low, high, 48000);
    const auto column = stereo.waveformSnapshot().columns.back();
    require(column.bandPower[0][0] > 0.9f && column.bandPower[1][2] > 0.9f,
            "left and right waveform colours analyze their own channel content");
    // The declared input format controls mono duplication even if a caller
    // supplies a non-null second pointer containing unrelated samples.
    AudioAnalyzer mono;
    feed(mono, low, high, 48000, 317, 1);
    const auto monoFrame = mono.snapshot();
    require(monoFrame.left == monoFrame.right && monoFrame.peakL == monoFrame.peakR &&
            monoFrame.rmsL == monoFrame.rmsR,
            "mono format ignores a conflicting right pointer throughout waveform and channel meters");
    const auto monoHistory = mono.waveformSnapshot();
    for (const auto& monoColumn : monoHistory.columns)
        require(monoColumn.minimum[0] == monoColumn.minimum[1] && monoColumn.maximum[0] == monoColumn.maximum[1] &&
                monoColumn.rms[0] == monoColumn.rms[1] && monoColumn.sumSquares[0] == monoColumn.sumSquares[1] &&
                monoColumn.bandEnergy[0] == monoColumn.bandEnergy[1] &&
                monoColumn.amplitudeCounts[0] == monoColumn.amplitudeCounts[1] &&
                monoColumn.minimumSample[0] == monoColumn.minimumSample[1] &&
                monoColumn.maximumSample[0] == monoColumn.maximumSample[1],
                "declared mono format duplicates all exact channel statistics and extrema timestamps");
    std::cout << "PASS frequency-derived waveform colour, independent stereo bands, and mono duplication\n";
}

void continuousWaveformWrapResetAndInvalid()
{
    AudioAnalyzer analyzer;
    std::vector<float> audio(48000 * 26, 0.0f);
    audio[9] = 1;
    audio[audio.size() - 3] = -0.75f;
    feed(analyzer, audio, audio, 48000, 997);
    const auto wrapped = analyzer.waveformSnapshot();
    checkWaveformFinite(wrapped);
    require(wrapped.columns.size() == 24250, "48 kHz history retains 24.25 seconds after wrapping");
    require(wrapped.columns.front().endSample == 48000 * 7 / 4 + 48, "ring evicts exactly the oldest complete buckets");
    require(wrapped.columns.back().endSample == audio.size(), "ring retains the newest complete bucket");
    near(wrapped.columns.back().minimum[0], -0.75, 0, "newest impulse survives ring wrapping");
    const auto oldCursor = wrapped.columns.back().sequence;
    require(analyzer.waveformSnapshot(oldCursor - 2).columns.size() == 2, "delta reads work across a wrapped ring");
    require(analyzer.waveformSnapshot(std::numeric_limits<std::uint64_t>::max()).columns.empty(),
            "future cursor safely produces an empty delta");
    analyzer.reset();
    const auto empty = analyzer.waveformSnapshot(oldCursor);
    require(empty.columns.empty() && empty.totalSamples == 0 && empty.generation > wrapped.generation,
            "reset clears waveform samples and publishes a new generation immediately");
    std::vector<float> silence(48, 0);
    feed(analyzer, silence, silence, 48000);
    const auto restarted = analyzer.waveformSnapshot(oldCursor);
    require(restarted.columns.size() == 1 && restarted.columns.front().sequence > oldCursor &&
            restarted.columns.front().endSample == 48, "existing cursor receives new generation with reset sample clock");
    near(restarted.columns.front().bandPower[0][0], 0, 0, "reset clears waveform filter tails");

    std::vector<float> newRate(441, 0.25f);
    analyzer.process(newRate.data(), nullptr, static_cast<int>(newRate.size()), 44100, 1);
    const auto reformatted = analyzer.waveformSnapshot(restarted.columns.back().sequence);
    require(reformatted.sampleRate == 44100 && reformatted.channelCount == 1 && reformatted.bucketSamples == 44 &&
            reformatted.totalSamples == 441 && reformatted.columns.size() == 10 && reformatted.tail.has_value() &&
            reformatted.generation > restarted.generation, "format change restarts waveform timing and bucket size");
    analyzer.process(nullptr, nullptr, 10, 44100, 1);
    analyzer.process(newRate.data(), nullptr, 10, 0, 1);
    analyzer.process(newRate.data(), nullptr, 10, 44100, 9);
    require(analyzer.waveformSnapshot().totalSamples == reformatted.totalSamples, "invalid input does not advance waveform time");

    analyzer.reset();
    std::vector<float> extreme(44 * 4, std::numeric_limits<float>::max());
    extreme[0] = std::numeric_limits<float>::infinity();
    extreme[1] = std::numeric_limits<float>::quiet_NaN();
    extreme[45] = std::numeric_limits<float>::lowest();
    analyzer.process(extreme.data(), nullptr, static_cast<int>(extreme.size()), 44100, 1);
    const auto safe = analyzer.waveformSnapshot();
    checkWaveformFinite(safe);
    near(safe.columns[0].minimum[0], 0, 0, "nonfinite samples are zero in waveform history");
    near(safe.columns[0].maximum[0], std::numeric_limits<float>::max(), 0, "large finite waveform samples are not clipped");
    require(safe.columns[0].rms[0] > std::numeric_limits<float>::max() * 0.9f,
            "double accumulators preserve RMS for extreme finite floats");
    for (const double rate : {8000.0, 44100.0, 48000.0, 96000.0, 192000.0, 384000.0})
    {
        const int bucket = static_cast<int>(std::llround(rate / 1000.0));
        std::vector<float> extremes(static_cast<std::size_t>(bucket * 5 + 1));
        for (std::size_t index = 0; index < extremes.size(); ++index)
            extremes[index] = index % 2 == 0 ? std::numeric_limits<float>::max() : std::numeric_limits<float>::lowest();
        extremes[0] = std::numeric_limits<float>::quiet_NaN();
        extremes[1] = std::numeric_limits<float>::infinity();
        AudioAnalyzer singleChunk, splitChunks;
        feed(singleChunk, extremes, extremes, rate, static_cast<int>(extremes.size()));
        feed(splitChunks, extremes, extremes, rate, 37);
        const auto single = singleChunk.waveformSnapshot(), split = splitChunks.waveformSnapshot();
        checkWaveformFinite(single); checkWaveformFinite(split);
        require(single.tail && split.tail && single.tail->sampleCount == 1 &&
                single.tail->bandEnergy == split.tail->bandEnergy && single.tail->sumSquares == split.tail->sumSquares,
                "extreme final partial sample stays finite and chunk-independent at every supported rate");
        require(single.tail->amplitudeCounts == split.tail->amplitudeCounts,
                "extreme partial amplitude counts do not depend on process chunking");
        for (std::size_t column = 0; column < single.columns.size(); ++column)
            require(single.columns[column].bandEnergy == split.columns[column].bandEnergy &&
                    single.columns[column].amplitudeCounts == split.columns[column].amplitudeCounts &&
                    single.columns[column].sumSquares == split.columns[column].sumSquares &&
                    single.columns[column].minimumSample == split.columns[column].minimumSample &&
                    single.columns[column].maximumSample == split.columns[column].maximumSample,
                    "extreme raw energies and extrema timing remain chunk-independent at every supported rate");
    }
    // Supported rates retain display history plus the buffered cursor margin.
    AudioAnalyzer lowRate;
    std::vector<float> slow(8000 * 25, 0.0f);
    lowRate.process(slow.data(), nullptr, static_cast<int>(slow.size()), 8000, 1);
    const auto lowRateHistory = lowRate.waveformSnapshot();
    require(lowRateHistory.columns.size() * lowRateHistory.bucketSamples >= 8000 * 24.25,
            "bucket storage retains at least 24.25 seconds at 8 kHz");
    checkWaveformFinite(lowRateHistory);
    std::cout << "PASS waveform ring wrap, reset and format generations, low-rate duration, invalid and extreme PCM\n";
}

// Independent direct-form-I reference using sine/cosine filter coefficients.
// Production uses bilinear-tangent coefficients and transposed direct form II.
// Testing a different realization catches coefficient, state and energy errors.
struct ReferenceWaveformFilter
{
    double b0, b1, b2, a1, a2;
    double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    ReferenceWaveformFilter(double rate, double cutoff, bool highPass)
    {
        const double omega = 2 * pi * cutoff / rate;
        const double cosine = std::cos(omega), alpha = std::sin(omega) / std::sqrt(2.0);
        const double denominator = 1 + alpha;
        b0 = (highPass ? 1 + cosine : 1 - cosine) * 0.5 / denominator;
        b1 = (highPass ? -2 : 2) * b0;
        b2 = b0;
        a1 = -2 * cosine / denominator;
        a2 = (1 - alpha) / denominator;
    }
    double process(double input)
    {
        const double output = b0 * input + b1 * x1 + b2 * x2 - a1 * y1 - a2 * y2;
        x2 = x1; x1 = input; y2 = y1; y1 = output;
        return output;
    }
};

struct ReferenceWaveformBands
{
    ReferenceWaveformFilter low, midHighPass, midLowPass, high;
    explicit ReferenceWaveformBands(double rate)
        : low(rate, 250, false), midHighPass(rate, 250, true),
          midLowPass(rate, std::min(4000.0, rate * 0.45), false),
          high(rate, std::min(4000.0, rate * 0.45), true) {}
    std::array<double, 3> process(double input)
    {
        return {low.process(input), midLowPass.process(midHighPass.process(input)), high.process(input)};
    }
};

void continuousWaveformAdditiveFilterReference()
{
    for (const double rate : {8000.0, 44100.0, 48000.0, 192000.0, 384000.0})
    {
        const int samples = static_cast<int>(rate * 0.12) + 13;
        std::vector<float> left(samples), right(samples);
        for (int sample = 0; sample < samples; ++sample)
        {
            const double time = sample / rate;
            if (time < 0.08)
            {
                left[sample] = static_cast<float>(0.3 * std::sin(2 * pi * 83 * time)
                    + 0.11 * std::sin(2 * pi * 1137 * time) + 0.03 * std::sin(2 * pi * rate * 0.34 * time));
                right[sample] = static_cast<float>(0.02 * std::sin(2 * pi * 103 * time + 0.3)
                    + 0.04 * std::sin(2 * pi * 727 * time) + 0.21 * std::sin(2 * pi * rate * 0.32 * time));
            }
        }
        left[static_cast<int>(rate * 0.079)] = 0.99f;
        right[static_cast<int>(rate * 0.079) + 1] = -0.87f;
        AudioAnalyzer analyzer;
        feed(analyzer, left, right, rate, 131);
        const auto history = analyzer.waveformSnapshot();
        checkWaveformFinite(history);
        auto columns = history.columns;
        if (history.tail) columns.push_back(*history.tail);
        std::array<ReferenceWaveformBands, 2> filters {ReferenceWaveformBands(rate), ReferenceWaveformBands(rate)};
        std::array<std::array<double, 3>, 2> referenceTotal {}, actualTotal {};
        bool observedRealFilterTail = false;
        std::uint64_t position = 0;
        for (const auto& column : columns)
        {
            std::array<std::array<double, 3>, 2> referenceEnergy {};
            std::array<double, 2> rawPower {};
            for (; position < column.endSample; ++position)
            {
                for (int channel = 0; channel < 2; ++channel)
                {
                    const double value = channel == 0 ? left[position] : right[position];
                    rawPower[channel] += value * value;
                    const auto bands = filters[channel].process(value);
                    for (int band = 0; band < 3; ++band) referenceEnergy[channel][band] += bands[band] * bands[band];
                }
            }
            for (int channel = 0; channel < 2; ++channel)
            {
                near(column.sumSquares[channel], rawPower[channel], 0, "raw sufficient statistic agrees exactly with direct PCM sum");
                for (int band = 0; band < 3; ++band)
                {
                    const double expected = referenceEnergy[channel][band];
                    near(column.bandEnergy[channel][band], expected, std::max(1.e-19, expected * 2.e-8),
                         "per-band energy agrees with independent direct-form-I reference at " + std::to_string(rate));
                    referenceTotal[channel][band] += expected;
                    actualTotal[channel][band] += column.bandEnergy[channel][band];
                    if (column.sumSquares[channel] == 0 && expected > 1.e-8)
                        observedRealFilterTail = true;
                }
            }
        }
        require(position == static_cast<std::uint64_t>(samples), "completed plus provisional intervals cover every source sample");
        require(observedRealFilterTail, "additive band energy preserves filter tails even when raw input power is zero");
        for (int channel = 0; channel < 2; ++channel)
            for (int band = 0; band < 3; ++band)
                near(actualTotal[channel][band], referenceTotal[channel][band],
                     std::max(1.e-19, referenceTotal[channel][band] * 2.e-8),
                     "regrouping raw band energies preserves reference energy instead of reconstructing from fractions");
    }
    std::cout << "PASS additive PCM/filter sufficient statistics against independent reference at five rates\n";
}

void continuousWaveformPartialTailsAndExtremaTiming()
{
    AudioAnalyzer analyzer;
    std::vector<float> left(47, 0), right(47, 0);
    left[46] = 1;
    right[45] = -0.5f;
    analyzer.process(left.data(), right.data(), 47, 48000);
    const auto partial = analyzer.waveformSnapshot();
    checkWaveformFinite(partial);
    require(partial.columns.empty() && partial.tail && partial.tail->sampleCount == 47,
            "stream stopping mid-bucket publishes its real tail without inventing a full column");
    require(partial.tail->maximumSample[0] == 46 && partial.tail->minimumSample[1] == 45,
            "final-sample impulses preserve their exact positions");
    near(partial.tail->maximum[0], 1, 0, "last incomplete-bucket transient is immediately visible");
    near(partial.tail->minimum[1], -0.5, 0, "right incomplete-bucket transient is immediately visible");
    near(partial.tail->sumSquares[0], 1, 0, "partial bucket reports true observed sample power");
    near(partial.tail->rms[0], std::sqrt(1.0 / 47.0), 1.e-8, "partial RMS uses observed count, never the full bucket size");
    const auto repeated = analyzer.waveformSnapshot(std::numeric_limits<std::uint64_t>::max());
    require(repeated.tail && repeated.tail->bandEnergy == partial.tail->bandEnergy &&
            repeated.tail->amplitudeCounts == partial.tail->amplitudeCounts &&
            repeated.tail->sumSquares == partial.tail->sumSquares,
            "repeated and future-cursor snapshots preserve the same provisional interval without processing it twice");
    const float completion = 0.25f;
    analyzer.process(&completion, &completion, 1, 48000);
    const auto completed = analyzer.waveformSnapshot();
    require(completed.columns.size() == 1 && !completed.tail && completed.columns[0].sequence > 0,
            "completion atomically promotes the partial interval into the ordinary delta history");
    near(completed.columns[0].sumSquares[0], 1.0625, 0, "promoted bucket contains each partial sample exactly once");
    require(completed.columns[0].amplitudeCounts[0][32] == 46 &&
            completed.columns[0].amplitudeCounts[0][64] == 1 &&
            completed.columns[0].amplitudeCounts[0][40] == 1,
            "partial-to-complete promotion counts each zero, impulse and final sample exactly once");
    require(completed.columns[0].maximumSample[0] == 46 && completed.columns[0].minimumSample[1] == 45,
            "bucket completion keeps original transient sample positions");
    std::vector<float> repeatedLevel(96, 0.4f);
    analyzer.process(repeatedLevel.data(), nullptr, 96, 48000);
    const auto tied = analyzer.waveformSnapshot(completed.columns[0].sequence);
    require(tied.columns.size() == 2 && tied.columns[0].minimumSample[0] == 48 &&
            tied.columns[0].maximumSample[0] == 48 && tied.columns[1].minimumSample[0] == 96 &&
            tied.columns[1].maximumSample[0] == 96, "extrema ties retain the earliest source position in every bucket");
    analyzer.process(&completion, nullptr, 1, 48000);
    require(analyzer.waveformSnapshot().tail.has_value(), "one actual sample can form a partial tail");
    analyzer.reset();
    require(!analyzer.waveformSnapshot().tail, "reset clears incomplete history too");
    std::cout << "PASS final partial-bucket impulses, promotion, exact extrema timing, and stable tie handling\n";
}

void continuousWaveformAmplitudeOccupancy()
{
    static_assert(WaveformColumn::amplitudeBinCount == 65, "65 bins preserve a centred zero-amplitude bin");
    // Exact bin-centre PCM: expected occupancy is known without duplicating the
    // implementation's quantizer. Opposite polarity must reflect all 65 bins.
    constexpr int samples = 65 * 48 + 17;
    std::vector<float> left(samples), right(samples);
    for (int sample = 0; sample < samples; ++sample)
    {
        left[sample] = static_cast<float>(sample % 65 - 32) / 32.0f;
        right[sample] = -left[sample];
    }
    AudioAnalyzer analyzer;
    feed(analyzer, left, right, 48000, 23);
    const auto history = analyzer.waveformSnapshot();
    checkWaveformFinite(history);
    auto columns = history.columns;
    require(history.tail.has_value(), "occupancy fixture includes a real partial bucket");
    columns.push_back(*history.tail);
    std::uint64_t countedSamples = 0;
    for (const auto& column : columns)
    {
        std::array<std::uint16_t, 65> expected {};
        for (auto sample = column.endSample - column.sampleCount; sample < column.endSample; ++sample)
            ++expected[sample % 65];
        require(column.amplitudeCounts[0] == expected, "occupancy counts exact amplitude-bin-centre PCM");
        for (std::size_t bin = 0; bin < 65; ++bin)
            require(column.amplitudeCounts[0][bin] == column.amplitudeCounts[1][64 - bin],
                    "opposite-polarity PCM reflects the symmetric amplitude bins");
        countedSamples += column.sampleCount;
    }
    require(countedSamples == samples, "complete and partial distributions account for the entire source exactly once");

    analyzer.reset();
    const std::vector<float> special {-2.f, -1.f, -.5f, 0.f, .5f, 1.f, 2.f,
        std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::infinity(),
        -std::numeric_limits<float>::infinity(), -1.f / 64.f, 1.f / 64.f};
    analyzer.process(special.data(), nullptr, static_cast<int>(special.size()), 48000, 1);
    const auto specialTail = *analyzer.waveformSnapshot().tail;
    std::array<std::uint16_t, 65> expectedSpecial {};
    expectedSpecial[0] = 2; expectedSpecial[16] = 1; expectedSpecial[32] = 5;
    expectedSpecial[33] = 1; expectedSpecial[48] = 1; expectedSpecial[64] = 2;
    require(specialTail.amplitudeCounts[0] == expectedSpecial && specialTail.amplitudeCounts[1] == expectedSpecial,
            "out-of-range amplitudes saturate end bins, nonfinite samples become zero, midpoint ties round upward");
    near(specialTail.minimum[0], -2, 0, "occupancy saturation does not clip measured negative extrema");
    near(specialTail.maximum[0], 2, 0, "occupancy saturation does not clip measured positive extrema");

    for (const float dc : {0.f, 0.375f, -0.375f})
    {
        analyzer.reset();
        std::vector<float> constant(384 * 3 + 5, dc);
        analyzer.process(constant.data(), nullptr, static_cast<int>(constant.size()), 384000, 1);
        const auto constantHistory = analyzer.waveformSnapshot();
        checkWaveformFinite(constantHistory);
        const std::size_t expectedBin = dc == 0.f ? 32 : dc > 0 ? 44 : 20;
        for (const auto& column : constantHistory.columns)
            require(column.amplitudeCounts[0][expectedBin] == 384 && column.amplitudeCounts[1][expectedBin] == 384,
                    "silence and DC occupy one bin without overflow at the maximum supported sample rate");
        require(constantHistory.tail && constantHistory.tail->amplitudeCounts[0][expectedBin] == 5,
                "reset and format changes clear prior amplitude counts before publishing a partial DC bucket");
    }
    std::cout << "PASS exact PCM amplitude occupancy, polarity symmetry, saturation, silence/DC and partial/reset conservation\n";
}
}

int main()
{
    silenceAndInvalidInput();
    spectrumAndPhase();
    loudnessCalibrationAndTimeWindows();
    callbackIndependenceAndReset();
    relativeAndAbsoluteGating();
    concurrentSnapshots();
    peakHoldsAndMonoLoudness();
    spectralProjectionEdges();
    spectralProjectionCustomRanges();
    spectralProjectionPreservesNarrowPeaks();
    continuousWaveformSamplesAndDeltas();
    continuousWaveformColourAndMono();
    continuousWaveformWrapResetAndInvalid();
    continuousWaveformAdditiveFilterReference();
    continuousWaveformPartialTailsAndExtremaTiming();
    continuousWaveformAmplitudeOccupancy();
    std::cout << "All analysis tests passed.\n";
}
