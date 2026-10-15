#include "Analysis.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <mutex>

namespace
{
constexpr double pi = 3.1415926535897932384626433832795;
constexpr int nFFT = AudioAnalyzer::fftSize;
constexpr int nBins = nFFT / 2 + 1;
constexpr int waveformSize = 1024;
constexpr int waveformHistoryStorage = 32768; // >= 24.25 s at every supported rate.
constexpr double histogramFloor = -70.0;
constexpr double histogramStep = 0.1;
constexpr int histogramSize = 8701; // -70 through +800 LUFS, bounded for any Float input.

float powerDB(double power)
{
    return static_cast<float>(std::max<double>(AudioAnalyzer::silenceDB,
                                               10.0 * std::log10(std::max(power, 1.0e-12))));
}

float loudness(double energy)
{
    return static_cast<float>(std::max<double>(AudioAnalyzer::silenceDB,
                           -0.691 + 10.0 * std::log10(std::max(energy, 1.0e-20))));
}

float visualEnergy(double meanSquare)
{
    // Deliberately artistic, calibrated against band RMS; not another meter.
    const auto db = powerDB(meanSquare);
    return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
}

struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    double s1 = 0, s2 = 0;

    double process(double input)
    {
        const double out = b0 * input + s1;
        s1 = b1 * input - a1 * out + s2;
        s2 = b2 * input - a2 * out;
        // Silence eventually clears tiny states, avoiding denormal slowdowns.
        if (std::abs(s1) < 1.0e-30) s1 = 0;
        if (std::abs(s2) < 1.0e-30) s2 = 0;
        return out;
    }
};

Biquad waveformFilter(double sampleRate, double cutoff, bool highPass)
{
    const double k = std::tan(pi * cutoff / sampleRate);
    const double denominator = 1.0 + std::sqrt(2.0) * k + k * k;
    Biquad filter;
    filter.b0 = (highPass ? 1.0 : k * k) / denominator;
    filter.b1 = (highPass ? -2.0 : 2.0) * filter.b0;
    filter.b2 = filter.b0;
    filter.a1 = 2.0 * (k * k - 1.0) / denominator;
    filter.a2 = (1.0 - std::sqrt(2.0) * k + k * k) / denominator;
    return filter;
}

struct WaveformBands
{
    Biquad low, midHighPass, midLowPass, high;

    explicit WaveformBands(double sampleRate = 48000.0)
    {
        const double upper = std::min(4000.0, sampleRate * 0.45);
        low = waveformFilter(sampleRate, 250.0, false);
        midHighPass = waveformFilter(sampleRate, 250.0, true);
        midLowPass = waveformFilter(sampleRate, upper, false);
        high = waveformFilter(sampleRate, upper, true);
    }

    std::array<double, 3> process(double input)
    {
        return {low.process(input), midLowPass.process(midHighPass.process(input)), high.process(input)};
    }
};

struct KWeighting
{
    Biquad shelf, highPass;

    explicit KWeighting(double sampleRate = 48000.0)
    {
        // Bilinear-transform parameters matching the BS.1770 filter at 48 kHz.
        // The high-pass numerator intentionally is [1,-2,1], with no /a0.
        const double shelfK = std::tan(pi * 1681.974450955533 / sampleRate);
        const double shelfQ = 0.7071752369554196;
        const double highGain = std::pow(10.0, 3.999843853973347 / 20.0);
        const double bandGain = std::pow(highGain, 0.4996667741545416);
        const double a0 = 1.0 + shelfK / shelfQ + shelfK * shelfK;
        shelf.b0 = (highGain + bandGain * shelfK / shelfQ + shelfK * shelfK) / a0;
        shelf.b1 = 2.0 * (shelfK * shelfK - highGain) / a0;
        shelf.b2 = (highGain - bandGain * shelfK / shelfQ + shelfK * shelfK) / a0;
        shelf.a1 = 2.0 * (shelfK * shelfK - 1.0) / a0;
        shelf.a2 = (1.0 - shelfK / shelfQ + shelfK * shelfK) / a0;

        const double highPassK = std::tan(pi * 38.13547087602444 / sampleRate);
        const double highPassQ = 0.5003270373238773;
        const double denominator = 1.0 + highPassK / highPassQ + highPassK * highPassK;
        highPass.b0 = 1.0;
        highPass.b1 = -2.0;
        highPass.b2 = 1.0;
        highPass.a1 = 2.0 * (highPassK * highPassK - 1.0) / denominator;
        highPass.a2 = (1.0 - highPassK / highPassQ + highPassK * highPassK) / denominator;
    }

    double process(double input) { return highPass.process(shelf.process(input)); }
};
}

struct AudioAnalyzer::State
{
    mutable std::mutex mutex;
    AnalysisFrame frame;
    std::uint64_t sequence = 0;
    std::uint64_t resetGeneration = 0;
    std::array<float, nFFT> inputL {}, inputR {};
    std::array<double, nFFT> window {}, realL {}, imagL {}, realR {}, imagR {};
    std::array<int, nFFT> reversed {};
    std::array<double, nFFT / 2> twiddleReal {}, twiddleImag {};
    double windowSum = 0, windowSquareSum = 0;
    int writeIndex = 0, filled = 0, hopSamples = 0;

    std::array<WaveformColumn, waveformHistoryStorage> waveformHistory {};
    std::uint64_t waveformSequence = 0, waveformTotalSamples = 0;
    int waveformWriteIndex = 0, waveformCount = 0, waveformCapacity = 24250;
    int waveformBucketSamples = 48, waveformBucketCount = 0;
    std::array<float, 2> waveformMinimum {}, waveformMaximum {};
    std::array<std::uint64_t, 2> waveformMinimumSample {}, waveformMaximumSample {};
    std::array<double, 2> waveformPower {};
    std::array<std::array<double, 3>, 2> waveformBandEnergy {};
    std::array<std::array<std::uint16_t, WaveformColumn::amplitudeBinCount>, 2> waveformAmplitudeCounts {};
    std::array<WaveformBands, 2> waveformBands {WaveformBands(), WaveformBands()};

    KWeighting kLeft, kRight;
    int meterWindow = 2400, meterSamples = 0;
    double meterPowerL = 0, meterPowerR = 0, meterCross = 0;
    double meterPeakL = 0, meterPeakR = 0;
    double heldPeakL = 0, heldPeakR = 0;
    int loudnessWindow = 4800, loudnessSamples = 0;
    double currentWeightedEnergy = 0;
    std::array<double, 30> energies {};
    int energyIndex = 0, energyCount = 0;
    std::array<double, histogramSize> histogramEnergies {}, histogramCounts {};
    double absoluteEnergySum = 0, absoluteCount = 0;

    State()
    {
        for (int i = 0; i < nFFT; ++i)
        {
            window[i] = 0.5 - 0.5 * std::cos(2.0 * pi * i / nFFT);
            windowSum += window[i];
            windowSquareSum += window[i] * window[i];
            int source = i, reverse = 0;
            for (int bit = 0; bit < 12; ++bit)
            {
                reverse = (reverse << 1) | (source & 1);
                source >>= 1;
            }
            reversed[i] = reverse;
        }
        for (int i = 0; i < nFFT / 2; ++i)
        {
            twiddleReal[i] = std::cos(-2.0 * pi * i / nFFT);
            twiddleImag[i] = std::sin(-2.0 * pi * i / nFFT);
        }
        clear(48000.0, 2);
    }

    void clear(double sampleRate, int channelCount)
    {
        frame = AnalysisFrame {};
        frame.sampleRate = sampleRate;
        frame.channelCount = channelCount;
        frame.left.assign(waveformSize, 0.0f);
        frame.right.assign(waveformSize, 0.0f);
        frame.spectrum.assign(nBins, AudioAnalyzer::silenceDB);
        frame.sequence = ++sequence;
        frame.resetGeneration = ++resetGeneration;
        inputL.fill(0); inputR.fill(0);
        writeIndex = filled = hopSamples = 0;
        kLeft = KWeighting(sampleRate); kRight = KWeighting(sampleRate);
        meterWindow = static_cast<int>(std::llround(sampleRate * 0.05));
        loudnessWindow = static_cast<int>(std::llround(sampleRate * 0.1));
        meterSamples = loudnessSamples = 0;
        meterPowerL = meterPowerR = meterCross = meterPeakL = meterPeakR = 0;
        heldPeakL = heldPeakR = 0;
        currentWeightedEnergy = 0;
        energies.fill(0);
        energyIndex = energyCount = 0;
        histogramEnergies.fill(0); histogramCounts.fill(0);
        absoluteEnergySum = absoluteCount = 0;
        waveformTotalSamples = 0;
        waveformWriteIndex = waveformCount = 0;
        waveformBucketSamples = std::max(1, static_cast<int>(std::llround(sampleRate / 1000.0)));
        waveformCapacity = std::min(waveformHistoryStorage,
            static_cast<int>(std::ceil(sampleRate * 24.25 / waveformBucketSamples)));
        waveformBands = {WaveformBands(sampleRate), WaveformBands(sampleRate)};
        clearWaveformBucket();
    }

    void clearWaveformBucket()
    {
        waveformBucketCount = 0;
        waveformMinimum.fill(std::numeric_limits<float>::max());
        waveformMaximum.fill(std::numeric_limits<float>::lowest());
        waveformMinimumSample.fill(waveformTotalSamples);
        waveformMaximumSample.fill(waveformTotalSamples);
        waveformPower.fill(0.0);
        for (auto& channel : waveformBandEnergy) channel.fill(0.0);
        for (auto& channel : waveformAmplitudeCounts) channel.fill(0);
    }

    void processWaveform(float left, float right)
    {
        const std::array<float, 2> input {left, right};
        for (int channel = 0; channel < 2; ++channel)
        {
            const auto sample = input[channel];
            if (waveformBucketCount == 0 || sample < waveformMinimum[channel])
            {
                waveformMinimum[channel] = sample;
                waveformMinimumSample[channel] = waveformTotalSamples;
            }
            if (waveformBucketCount == 0 || sample > waveformMaximum[channel])
            {
                waveformMaximum[channel] = sample;
                waveformMaximumSample[channel] = waveformTotalSamples;
            }
            const double value = sample;
            waveformPower[channel] += value * value;
            // Non-negative rounding to the nearest amplitude-bin centre.
            // The source is already finite; only histogram indexing clamps.
            constexpr double halfBins = (WaveformColumn::amplitudeBinCount - 1) * 0.5;
            const auto bin = static_cast<std::size_t>(
                (static_cast<double>(std::clamp(sample, -1.0f, 1.0f)) + 1.0) * halfBins + 0.5);
            ++waveformAmplitudeCounts[channel][bin];
            const auto bands = waveformBands[channel].process(value);
            for (int band = 0; band < 3; ++band)
                waveformBandEnergy[channel][band] += bands[band] * bands[band];
        }
        ++waveformTotalSamples;
        if (++waveformBucketCount != waveformBucketSamples) return;

        waveformHistory[waveformWriteIndex] = makeWaveformColumn(++waveformSequence);
        waveformWriteIndex = (waveformWriteIndex + 1) % waveformCapacity;
        waveformCount = std::min(waveformCapacity, waveformCount + 1);
        clearWaveformBucket();
    }

    WaveformColumn makeWaveformColumn(std::uint64_t columnSequence) const
    {
        WaveformColumn column;
        column.sequence = columnSequence;
        column.endSample = waveformTotalSamples;
        column.sampleCount = waveformBucketCount;
        column.minimum = waveformMinimum;
        column.maximum = waveformMaximum;
        column.minimumSample = waveformMinimumSample;
        column.maximumSample = waveformMaximumSample;
        column.sumSquares = waveformPower;
        column.bandEnergy = waveformBandEnergy;
        column.amplitudeCounts = waveformAmplitudeCounts;
        for (int channel = 0; channel < 2; ++channel)
        {
            // All accumulators are double, so even FLT_MAX input remains finite.
            column.rms[channel] = static_cast<float>(std::min<double>(std::numeric_limits<float>::max(),
                std::sqrt(waveformPower[channel] / waveformBucketCount)));
            const auto& energy = waveformBandEnergy[channel];
            const double sum = energy[0] + energy[1] + energy[2];
            for (int band = 0; band < 3; ++band)
                column.bandPower[channel][band] = sum > 0.0 ? static_cast<float>(energy[band] / sum) : 0.0f;
        }
        return column;
    }

    void transform(std::array<double, nFFT>& real, std::array<double, nFFT>& imag)
    {
        // Iterative radix-2 transform. Input is already in bit-reversed order.
        for (int length = 2; length <= nFFT; length *= 2)
        {
            const int half = length / 2;
            const int stride = nFFT / length;
            for (int start = 0; start < nFFT; start += length)
                for (int j = 0; j < half; ++j)
                {
                    const int a = start + j, b = a + half;
                    const int twiddle = j * stride;
                    const double tr = real[b] * twiddleReal[twiddle] - imag[b] * twiddleImag[twiddle];
                    const double ti = real[b] * twiddleImag[twiddle] + imag[b] * twiddleReal[twiddle];
                    real[b] = real[a] - tr;
                    imag[b] = imag[a] - ti;
                    real[a] += tr;
                    imag[a] += ti;
                }
        }
    }

    void updateSpectrum()
    {
        for (int i = 0; i < nFFT; ++i)
        {
            const int source = (writeIndex + i) % nFFT;
            const int dest = reversed[i];
            realL[dest] = inputL[source] * window[i];
            realR[dest] = inputR[source] * window[i];
            imagL[dest] = imagR[dest] = 0;
        }
        transform(realL, imagL);
        transform(realR, imagR);
        double low = 0, mid = 0, high = 0;
        for (int bin = 0; bin < nBins; ++bin)
        {
            const double leftPower = realL[bin] * realL[bin] + imagL[bin] * imagL[bin];
            const double rightPower = realR[bin] * realR[bin] + imagR[bin] * imagR[bin];
            const double stereoPower = (leftPower + rightPower) * 0.5;
            const double sidedness = (bin == 0 || bin == nFFT / 2) ? 1.0 : 2.0;
            // Coherent-gain normalization: a bin-centred full-scale sine is 0 dBFS.
            frame.spectrum[bin] = powerDB(stereoPower * sidedness * sidedness / (windowSum * windowSum));
            // Parseval/window-energy normalization for total band mean square.
            const double bandPower = stereoPower * sidedness / (nFFT * windowSquareSum);
            const double frequency = bin * frame.sampleRate / nFFT;
            if (frequency >= 20.0 && frequency < 250.0) low += bandPower;
            else if (frequency >= 250.0 && frequency < 4000.0) mid += bandPower;
            else if (frequency >= 4000.0 && frequency <= 20000.0) high += bandPower;
        }
        frame.low = visualEnergy(low);
        frame.mid = visualEnergy(mid);
        frame.high = visualEnergy(high);
        for (int i = 0; i < waveformSize; ++i)
        {
            const int source = (writeIndex + nFFT - waveformSize + i) % nFFT;
            frame.left[i] = inputL[source];
            frame.right[i] = inputR[source];
        }
        frame.sequence = ++sequence;
    }

    void updateMeters()
    {
        frame.peakL = powerDB(meterPeakL * meterPeakL);
        frame.peakR = powerDB(meterPeakR * meterPeakR);
        frame.rmsL = powerDB(meterPowerL / meterSamples);
        frame.rmsR = powerDB(meterPowerR / meterSamples);
        const double denominator = std::sqrt(meterPowerL * meterPowerR);
        frame.correlation = denominator > 1.0e-20
            ? static_cast<float>(std::clamp(meterCross / denominator, -1.0, 1.0)) : 0.0f;
        meterSamples = 0;
        meterPowerL = meterPowerR = meterCross = meterPeakL = meterPeakR = 0;
    }

    double averageRecentEnergy(int count) const
    {
        double sum = 0;
        for (int i = 0; i < count; ++i)
            sum += energies[(energyIndex + 29 - i) % 30];
        return sum / count;
    }

    void updateIntegrated(double energy)
    {
        const double level = -0.691 + 10.0 * std::log10(std::max(energy, 1.0e-20));
        if (level > histogramFloor)
        {
            const int bucket = std::clamp(static_cast<int>(std::floor((level - histogramFloor) / histogramStep)),
                                          0, histogramSize - 1);
            histogramEnergies[bucket] += energy;
            histogramCounts[bucket] += 1.0;
            absoluteEnergySum += energy;
            absoluteCount += 1.0;
        }
        if (absoluteCount == 0) return;

        const double relativeThresholdEnergy = (absoluteEnergySum / absoluteCount) * 0.1;
        double acceptedEnergy = 0, acceptedCount = 0;
        for (int i = 0; i < histogramSize; ++i)
        {
            if (histogramCounts[i] > 0 &&
                histogramEnergies[i] / histogramCounts[i] > relativeThresholdEnergy)
            {
                acceptedEnergy += histogramEnergies[i];
                acceptedCount += histogramCounts[i];
            }
        }
        if (acceptedCount > 0)
            frame.integratedLUFS = loudness(acceptedEnergy / acceptedCount);
    }

    void updateLoudness()
    {
        energies[energyIndex] = currentWeightedEnergy / loudnessSamples;
        energyIndex = (energyIndex + 1) % 30;
        energyCount = std::min(30, energyCount + 1);
        loudnessSamples = 0;
        currentWeightedEnergy = 0;
        if (energyCount >= 4)
        {
            const double momentaryEnergy = averageRecentEnergy(4);
            frame.momentaryLUFS = loudness(momentaryEnergy);
            updateIntegrated(momentaryEnergy);
        }
        if (energyCount >= 30)
            frame.shortTermLUFS = loudness(averageRecentEnergy(30));
    }
};

AudioAnalyzer::AudioAnalyzer() : state(std::make_unique<State>()) {}
AudioAnalyzer::~AudioAnalyzer() = default;

void AudioAnalyzer::reset()
{
    std::lock_guard<std::mutex> guard(state->mutex);
    state->clear(state->frame.sampleRate, state->frame.channelCount);
}

AnalysisFrame AudioAnalyzer::snapshot() const
{
    std::lock_guard<std::mutex> guard(state->mutex);
    return state->frame;
}

WaveformHistorySnapshot AudioAnalyzer::waveformSnapshot(std::uint64_t afterSequence) const
{
    std::lock_guard<std::mutex> guard(state->mutex);
    const auto& s = *state;
    WaveformHistorySnapshot result;
    result.generation = s.resetGeneration;
    result.totalSamples = s.waveformTotalSamples;
    result.sampleRate = s.frame.sampleRate;
    result.bucketSamples = s.waveformBucketSamples;
    result.channelCount = s.frame.channelCount;
    if (s.waveformBucketCount > 0) result.tail = s.makeWaveformColumn(0);
    const int first = (s.waveformWriteIndex + s.waveformCapacity - s.waveformCount) % s.waveformCapacity;
    // Sequence numbers are contiguous even across ring wraps; use arithmetic to
    // skip old columns without walking the full ring on every display refresh.
    const auto oldestSequence = s.waveformCount > 0 ? s.waveformHistory[first].sequence : 0;
    const auto skipped = afterSequence >= oldestSequence && s.waveformCount > 0
        ? std::min<std::uint64_t>(static_cast<std::uint64_t>(s.waveformCount), afterSequence - oldestSequence + 1) : 0;
    result.columns.reserve(static_cast<std::size_t>(s.waveformCount) - skipped);
    for (int index = static_cast<int>(skipped); index < s.waveformCount; ++index)
        result.columns.push_back(s.waveformHistory[(first + index) % s.waveformCapacity]);
    return result;
}

void AudioAnalyzer::process(const float* left, const float* right, int count, double sampleRate,
                            int channelCount)
{
    if (left == nullptr || count <= 0 || !std::isfinite(sampleRate) || sampleRate < 8000.0 || sampleRate > 384000.0
        || (channelCount != 1 && channelCount != 2))
        return;
    std::lock_guard<std::mutex> guard(state->mutex);
    auto& s = *state;
    if (sampleRate != s.frame.sampleRate || channelCount != s.frame.channelCount)
        s.clear(sampleRate, channelCount);

    for (int i = 0; i < count; ++i)
    {
        const float l = std::isfinite(left[i]) ? left[i] : 0.0f;
        const float rawR = channelCount == 2 && right != nullptr ? right[i] : left[i];
        const float r = std::isfinite(rawR) ? rawR : 0.0f;
        s.inputL[s.writeIndex] = l;
        s.inputR[s.writeIndex] = r;
        s.writeIndex = (s.writeIndex + 1) % nFFT;
        s.filled = std::min(nFFT, s.filled + 1);
        ++s.hopSamples;

        const double dl = l, dr = r;
        s.processWaveform(l, r);
        s.meterPowerL += dl * dl;
        s.meterPowerR += dr * dr;
        s.meterCross += dl * dr;
        s.meterPeakL = std::max(s.meterPeakL, std::abs(dl));
        s.meterPeakR = std::max(s.meterPeakR, std::abs(dr));
        s.heldPeakL = std::max(s.heldPeakL, std::abs(dl));
        s.heldPeakR = std::max(s.heldPeakR, std::abs(dr));
        if (++s.meterSamples == s.meterWindow) s.updateMeters();

        const double weightedL = s.kLeft.process(dl), weightedR = s.kRight.process(dr);
        s.currentWeightedEnergy += weightedL * weightedL + (channelCount == 2 ? weightedR * weightedR : 0.0);
        if (++s.loudnessSamples == s.loudnessWindow) s.updateLoudness();

        if (s.filled == nFFT && s.hopSamples >= fftHop)
        {
            s.updateSpectrum();
            s.hopSamples = 0;
        }
    }
    s.frame.peakHoldL = powerDB(s.heldPeakL * s.heldPeakL);
    s.frame.peakHoldR = powerDB(s.heldPeakR * s.heldPeakR);
}
