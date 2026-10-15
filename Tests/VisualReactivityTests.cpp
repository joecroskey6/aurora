#include "VisualReactivity.h"
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) { std::cerr << "FAIL: " << message << '\n'; std::exit(1); }
}
float encoded(float db) { return std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f); }
AnalysisFrame frame(float low, float mid, float high, std::uint64_t sequence) {
    AnalysisFrame f;
    f.low = encoded(low); f.mid = encoded(mid); f.high = encoded(high); f.sequence = sequence;
    return f;
}
void bounded(const VisualReaction& v) {
    for (float x : {v.low, v.mid, v.high, v.onset, v.activity})
        require(std::isfinite(x) && x >= 0 && x <= 1, "all controls remain finite and bounded");
}
double difference(const VisualReaction& a, const VisualReaction& b) {
    return std::max({std::abs(a.low-b.low), std::abs(a.mid-b.mid), std::abs(a.high-b.high),
                     std::abs(a.onset-b.onset), std::abs(a.activity-b.activity)});
}
void silenceAndQuiet() {
    VisualReactivity tracker;
    for (int i = 0; i < 200; ++i) {
        auto output = tracker.update(frame(-120,-120,-120,i+1),1.0/30);
        require(difference(output, {}) == 0, "silence has no activity, onset or motion");
    }
    for (int i = 0; i < 200; ++i) {
        auto output = tracker.update(frame(-59,-120,-120,i+201),1.0/30);
        require(difference(output, {}) == 0, "quiet gate prevents sub-threshold normalization");
    }
}
void volumeInvariance() {
    VisualReactivity loud, quiet;
    double worst = 0;
    for (int i = 0; i < 360; ++i) {
        const float bass = -17 + 7 * std::sin(float(i) * .11f);
        const float middle = -21 + 5 * std::sin(float(i) * .19f + 1);
        const float treble = -25 + 6 * std::sin(float(i) * .23f + 2);
        auto a = loud.update(frame(bass,middle,treble,i+1),1.0/30);
        auto b = quiet.update(frame(bass-16,middle-16,treble-16,i+1),1.0/30);
        bounded(a); bounded(b); worst = std::max(worst,difference(a,b));
    }
    require(worst < 0.0001, "16dB level change preserves normalized musical response above quiet gate");
}
void transientsAndSequence() {
    VisualReactivity tracker;
    for (int i = 0; i < 90; ++i) tracker.update(frame(-34,-40,-44,i+1),1.0/30);
    auto before = tracker.snapshot();
    auto attack = tracker.update(frame(-14,-40,-44,91),1.0/30);
    require(attack.onset > .95f, "large bass attack creates a strong onset");
    require(attack.low > before.low + .2f, "relative energy gives bass attack visible contrast");
    require(attack.mid < attack.low, "bass transient does not drive all bands equally");
    auto heldFrame = frame(-14,-40,-44,91);
    float previous = attack.onset;
    for (int i = 0; i < 5; ++i) {
        const auto held = tracker.update(heldFrame,1.0/60);
        require(held.onset < previous, "duplicate FFT snapshot cannot retrigger onset");
        previous = held.onset;
    }
    for (int i = 0; i < 180; ++i) tracker.update(frame(-14,-40,-44,92+i),1.0/30);
    require(tracker.snapshot().onset < .0001f, "steady sustained sound is not continually treated as an onset");
    require(tracker.snapshot().low > .5f, "sustained music retains a useful visible body");
    auto release = tracker.update(frame(-120,-120,-120,300),1.0/30);
    require(release.low > 0, "release is smooth rather than an abrupt stop");
    for (int i = 0; i < 90; ++i) tracker.update(frame(-120,-120,-120,301+i),1.0/30);
    require(difference(tracker.snapshot(),{}) == 0, "silence eventually closes all controls exactly");
    VisualReactivity newPassage;
    const auto quietStart = frame(-38,-43,-47,400);
    const auto resumed = tracker.update(quietStart,1.0/30);
    const auto fresh = newPassage.update(quietStart,1.0/30);
    require(difference(resumed,fresh) < .00001,
            "quiet passage after silence is not suppressed by an earlier loud reference");
}
VisualReaction steadyAtRate(int fps, double seconds) {
    VisualReactivity tracker;
    // Establish one common observation time for the initial onset. Starting at
    // the first GUI tick would place that event at 50ms for20Hz but8ms for120Hz,
    // measuring sampling latency rather than the time constants that follow it.
    tracker.update(frame(-24,-30,-36,1),1.0/30);
    const int count = int(std::lround(seconds * fps));
    for (int i = 0; i < count; ++i) {
        // The FFT sequence is independent of GUI cadence, including repeats at
        // high frame rates and skipped FFTs at low frame rates.
        const auto sequence = std::uint64_t(std::floor((double(i)+1)/fps*48000/1024))+2;
        tracker.update(frame(-24,-30,-36,sequence),1.0/fps);
    }
    return tracker.snapshot();
}
void cadence() {
    const auto reference = steadyAtRate(30,1.0);
    for (int fps : {20, 60, 120})
        require(difference(reference,steadyAtRate(fps,1.0)) < .0005,
                "attack/release/reference tracking uses elapsed time across GUI cadences");
    // Identical pulses aligned on all clocks, then compare the settled release.
    auto pulse = [](int fps) {
        VisualReactivity tracker;
        for (int i = 0; i < fps * 3; ++i) {
            const bool hit = i >= fps && i < fps + fps/5;
            const auto seq = std::uint64_t(std::floor((double(i)+1)/fps*48000/1024))+1;
            tracker.update(frame(hit ? -14 : -34,-40,-44,seq),1.0/fps);
        }
        return tracker.snapshot();
    };
    require(difference(pulse(20),pulse(120)) < .025,
            "pulse recovery remains comparable at20Hz and120Hz");
}
void staleFreezeAndReset() {
    VisualReactivity tracker;
    for (int i = 0; i < 60; ++i) tracker.update(frame(-18,-25,-30,i+1),1.0/30);
    const auto frozen = tracker.snapshot();
    for (int i = 0; i < 100; ++i)
        require(difference(frozen,tracker.snapshot()) == 0, "freeze by skipping update preserves exact state");
    auto stale = frame(-18,-25,-30,60);
    for (int i = 0; i < 120; ++i) tracker.update(stale,1.0/30);
    require(difference(tracker.snapshot(),{}) == 0, "stale FFT data cannot keep motion alive indefinitely");
    tracker.reset();
    require(difference(tracker.snapshot(),{}) == 0, "manual reset clears every feature");
    tracker.update(frame(-18,-25,-30,100),1.0/30);
    auto resetFrame = frame(-120,-120,-120,101); resetFrame.resetGeneration=1;
    require(difference(tracker.update(resetFrame,1.0/30),{}) == 0, "generation reset clears previous music immediately");
    tracker.update(frame(-18,-25,-30,102),1.0/30);
    auto rateFrame = frame(-120,-120,-120,103); rateFrame.sampleRate=96000;
    require(difference(tracker.update(rateFrame,1.0/30),{}) == 0, "sample-rate reconfiguration clears history");
    tracker.update(frame(-18,-25,-30,104),1.0/30);
    auto monoFrame = frame(-120,-120,-120,105); monoFrame.channelCount=1;
    require(difference(tracker.update(monoFrame,1.0/30),{}) == 0, "channel reconfiguration clears history");
}
void invalidValues() {
    VisualReactivity tracker;
    auto f = frame(-20,-20,-20,1);
    f.low=std::numeric_limits<float>::quiet_NaN(); f.mid=std::numeric_limits<float>::infinity(); f.high=-10;
    bounded(tracker.update(f,1.0/30));
    const auto before=tracker.snapshot();
    require(difference(before,tracker.update(f,std::numeric_limits<double>::quiet_NaN()))==0,
            "invalid elapsed time does not mutate state");
    require(difference(before,tracker.update(f,-1))==0,"negative elapsed time is ignored");
    f.sequence=2; f.low=100; f.mid=2; f.high=1;
    bounded(tracker.update(f,1000));
}

// A real PCM path, including the analyzer's 4096-sample Hann window and1024-hop
// cadence. The soft sustained tune has slow amplitude changes; separate bass
// drum/snare envelopes must produce stronger visual attacks at either gain.
std::vector<VisualReaction> musicalPCM(bool drums, float gain) {
    constexpr double rate = 48000.0, pi = 3.14159265358979323846;
    constexpr int guiFrames = 180, samplesPerGUI = 1600;
    AudioAnalyzer analyzer;
    VisualReactivity tracker;
    std::vector<VisualReaction> reactions;
    std::vector<float> left(samplesPerGUI), right(samplesPerGUI);
    const double hits[] {1.25,2.0,2.75,3.5,4.25,5.0};
    for (int tick = 0; tick < guiFrames; ++tick) {
        for (int sample = 0; sample < samplesPerGUI; ++sample) {
            const int n = tick * samplesPerGUI + sample;
            const double t = n / rate;
            const double breathing = 0.85 + 0.15 * std::sin(2*pi*0.22*t);
            // Even the quiet version remains above the documented -46dB gate;
            // this tests level normalization rather than intentionally gated noise.
            const double tune = breathing * (0.036*std::sin(2*pi*440*t)
                + 0.021*std::sin(2*pi*660*t) + 0.0135*std::sin(2*pi*880*t));
            double percussion = 0;
            if (drums) {
                for (double hit : hits) {
                    const double age = t - hit;
                    if (age >= 0 && age < .40) {
                        const double envelope = (1-std::exp(-age/.002))*std::exp(-age/.080);
                        // An analytic downward pitch sweep; phase is its integral.
                        const double phase = 2*pi*(48*age + 48*.027*(1-std::exp(-age/.027)));
                        percussion += .36*envelope*std::sin(phase);
                    }
                    const double snareAge = t - hit - .375;
                    if (snareAge >= 0 && snareAge < .20) {
                        std::uint32_t hash = std::uint32_t(n) * 747796405u + 2891336453u;
                        hash = ((hash >> ((hash >> 28u) + 4u)) ^ hash) * 277803737u;
                        hash = (hash >> 22u) ^ hash;
                        const double noise = double(hash & 65535u) / 32767.5 - 1;
                        percussion += .075*noise*(1-std::exp(-snareAge/.001))*std::exp(-snareAge/.035);
                    }
                }
            }
            const double bass = .009*std::sin(2*pi*90*t);
            left[std::size_t(sample)] = gain*float(tune+bass+percussion);
            right[std::size_t(sample)] = gain*float(tune*.94+bass+percussion);
        }
        // Feed realistic small host blocks while GUI observations remain30Hz.
        for (int offset = 0; offset < samplesPerGUI; offset += 256) {
            const int count = std::min(256,samplesPerGUI-offset);
            analyzer.process(left.data()+offset,right.data()+offset,count,rate);
        }
        const auto meters = analyzer.snapshot();
        require(meters.peakL < 0 && meters.peakR < 0, "PCM test material stays below full scale");
        const auto response = tracker.update(meters,1.0/30);
        bounded(response);
        reactions.push_back(response);
    }
    return reactions;
}

void pcmMusicResponse() {
    const auto tune = musicalPCM(false,1);
    const auto drums = musicalPCM(true,1);
    const auto quietDrums = musicalPCM(true,0.25118864f); // Same PCM at-12dB.
    float tuneOnset = 0;
    for (std::size_t i = 30; i < tune.size(); ++i) tuneOnset=std::max(tuneOnset,tune[i].onset);
    require(tuneOnset < .20f, "smooth real PCM tune is not mistaken for repeated drum attacks");
    double largestLevelDifference = 0;
    for (double hit : {1.25,2.0,2.75,3.5,4.25,5.0}) {
        float peakOnset = 0, quietPeak = 0, bassMotion = 0;
        for (std::size_t i = 0; i < drums.size(); ++i) {
            const double time = (double(i)+1)/30.0;
            if (time < hit || time > hit+.25) continue;
            peakOnset=std::max(peakOnset,drums[i].onset);
            quietPeak=std::max(quietPeak,quietDrums[i].onset);
            bassMotion=std::max(bassMotion,drums[i].low-tune[i].low);
            largestLevelDifference=std::max(largestLevelDifference,
                double(std::max(std::abs(drums[i].onset-quietDrums[i].onset),
                                std::abs(drums[i].low-quietDrums[i].low))));
        }
        require(peakOnset > .65f && quietPeak > .65f,
                "each actual PCM kick produces a strong onset at both input volumes");
        require(bassMotion > .20f, "actual PCM drums produce visible bass motion beyond the sustained tune");
    }
    require(largestLevelDifference < .025,
            "real analyzer-to-visual bass and attack responses survive a12dB volume change");
    std::cout << "PASS real PCM tune/drums: quiet and loud attacks agree within "
              << largestLevelDifference << '\n';
}
}
int main() {
    silenceAndQuiet(); volumeInvariance(); transientsAndSequence(); cadence();
    staleFreezeAndReset(); invalidValues(); pcmMusicResponse();
    std::cout << "Visual reactivity tests passed\n";
}
