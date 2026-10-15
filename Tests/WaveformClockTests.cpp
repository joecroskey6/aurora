#include "WaveformClock.h"

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <string>

namespace {
int checks = 0;
void require(bool condition, const std::string& message) {
    ++checks;
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        std::exit(1);
    }
}
void bounded(double cursor, std::uint64_t completed) {
    require(std::isfinite(cursor) && cursor >= 0.0
            && cursor <= static_cast<double>(completed),
            "cursor must be finite and remain inside completed real audio");
}
bool close(double a, double b) { return std::abs(a - b) < 1.0e-7; }
std::uint64_t delivered(double time, double rate, int block) {
    return static_cast<std::uint64_t>(std::floor(time * rate / block)) * block;
}
std::uint64_t complete(std::uint64_t total) { return total / 48 * 48; }

void steadyBlocks() {
    for (double rate : {44100.0, 48000.0, 96000.0}) {
        for (int block : {64, 128, 512, 1024, 2048}) {
            WaveformClock clock;
            double previous = 0.0, worstSpeedError = 0.0, lagSum = 0.0;
            int lagCount = 0;
            bool sawFractional = false;
            for (int frame = 0; frame < 720; ++frame) {
                const double now = frame / 60.0;
                const auto total = delivered(now, rate, block);
                const auto end = complete(total);
                const double cursor = clock.update(total, end, rate, 1, now, false);
                bounded(cursor, end);
                require(cursor >= previous, "steady playback never reverses the display timeline");
                if (frame > 120) {
                    const double speed = (cursor - previous) * 60.0 / rate;
                    worstSpeedError = std::max(worstSpeedError, std::abs(speed - 1.0));
                    lagSum += (now * rate - cursor) / rate;
                    ++lagCount;
                    sawFractional |= std::abs(cursor - std::round(cursor)) > 0.001;
                }
                previous = cursor;
            }
            require(worstSpeedError < 0.07,
                    "60 Hz cursor movement stays smooth across audio block sizes");
            const double averageLag = lagSum / lagCount;
            require(averageLag >= 0.065 && averageLag <= 0.125,
                    "display settles near the intended 80 ms buffer plus block delivery latency");
            require(sawFractional, "cursor preserves sub-sample precision for smooth scrolling");
        }
    }
}

void jitterAndBursts() {
    WaveformClock clock;
    constexpr double rate = 48000;
    const double intervals[] = {.012, .020, .015, .019, .017, .010, .024};
    double now = 0, previous = 0;
    std::uint64_t total = 0;
    for (int frame = 0; frame < 700; ++frame) {
        const double dt = intervals[frame % 7];
        now += dt;
        // 50 ms delivery bursts, including several GUI paints per snapshot.
        total = delivered(now, rate, 2400);
        const auto end = complete(total);
        const double cursor = clock.update(total, end, rate, 3, now, false);
        bounded(cursor, end);
        require(cursor >= previous, "jitter and delivery bursts do not rewind history");
        if (frame > 100) {
            const double speed = (cursor - previous) / (rate * dt);
            require(speed >= 0.879 && speed <= 1.121,
                    "phase correction cannot create block-sized jumps or pauses during playback");
        }
        previous = cursor;
    }
    const double beforeSameTime = previous;
    previous = clock.update(total + 100, complete(total + 100), rate, 3, now, false);
    require(previous == beforeSameTime, "a duplicate GUI timestamp cannot move the cursor");
}

void stopsAndResumes() {
    WaveformClock clock;
    constexpr double rate = 48000;
    const std::uint64_t total = 48013, end = 48000;
    double previous = clock.update(total, end, rate, 1, 1.0, false);
    require(previous < end, "initial display cursor includes the requested lag");
    for (int frame = 1; frame <= 120; ++frame) {
        const double cursor = clock.update(total, end, rate, 1, 1.0 + frame / 60.0, false);
        bounded(cursor, end);
        require(cursor >= previous, "a stopped stream consumes its buffered tail only forwards");
        if (frame >= 10)
            require(cursor == end, "stopped input holds exactly at its final completed sample");
        previous = cursor;
    }
    require(clock.update(total, end, rate, 1, 1000000.0, false) == end,
            "long stalls cannot scroll into invented history");
    for (int frame = 1; frame <= 30; ++frame) {
        const auto nextTotal = total + frame * 800;
        const auto nextEnd = complete(nextTotal);
        const double cursor = clock.update(nextTotal, nextEnd, rate, 1,
                                            1000000.0 + frame / 60.0, false);
        bounded(cursor, nextEnd);
        require(cursor >= previous, "stream resumption refills its delay without rewinding");
        if (frame <= 4)
            require(cursor == end, "resumed PCM builds the display buffer before scrolling");
        previous = cursor;
    }
    require(previous > end + 16000, "resumed input eventually scrolls smoothly again");

    WaveformClock empty;
    for (int frame = 0; frame < 120; ++frame)
        require(empty.update(0, 0, rate, 1, frame / 60.0, false) == 0,
                "an empty stream never creates a moving cursor");
}

void freezeAndDiscontinuities() {
    WaveformClock clock;
    constexpr double rate = 48000;
    const double held = clock.update(48000, 48000, rate, 1, 1.0, false);
    for (int frame = 1; frame <= 120; ++frame)
        require(clock.update(48000 + frame * 800, 48000 + frame * 800,
                             rate, 1, 1.0 + frame / 60.0, true) == held,
                "freeze holds exactly while incoming PCM continues");
    const double resumed = clock.update(144000, 144000, rate, 1, 3.0, false);
    require(close(resumed, 144000 - rate * WaveformClock::displayLagSeconds),
            "unfreeze reanchors at the live delayed cursor");
    require(clock.update(2000, 1968, rate, 2, 3.1, false) == 0,
            "a new stream generation resets the display buffer");
    require(close(clock.update(48000, 48000, 96000, 2, 4.0, false), 40320),
            "sample-rate changes recalculate the display lag");
    require(close(clock.update(9600, 9600, 96000, 2, 4.1, false), 1920),
            "a sample-counter reset reanchors safely");
    require(close(clock.update(9600, 9600, 96000, 2, 2.0, false), 1920),
            "a backwards GUI clock reanchors instead of creating negative motion");
    require(clock.update(10000, 20000, 96000, 2, 2.1, false) <= 10000,
            "invalid completed bounds cannot outrun total observed audio");
    clock.reset();
    require(clock.update(0, 0, rate, 1, 0, false) == 0,
            "explicit reset discards all timing state");
    require(clock.update(48000, 48000, rate, 1, 1, true) == 0,
            "freezing a previously empty clock holds its original cursor");
}

void invalidInputs() {
    for (double bad : {std::numeric_limits<double>::quiet_NaN(),
                       std::numeric_limits<double>::infinity(),
                       -std::numeric_limits<double>::infinity()}) {
        WaveformClock clock;
        require(clock.update(50000, 50000, 48000, 1, bad, false) == 0,
                "an invalid initial wall time returns a finite empty cursor");
        const double held = clock.update(50000, 50000, 48000, 1, 2, false);
        require(clock.update(55000, 55000, 48000, 1, bad, false) == held,
                "invalid wall time cannot corrupt a valid cursor");
        require(clock.update(55000, 55000, bad, 1, 3, false) == 0,
                "non-finite sample rates reset safely");
    }
    WaveformClock clock;
    require(clock.update(50000, 50000, 0, 1, 1, false) == 0,
            "zero sample rates cannot divide or advance");
    require(clock.update(50000, 50000, -48000, 1, 1, false) == 0,
            "negative sample rates cannot reverse time");
    const auto enormous = std::numeric_limits<std::uint64_t>::max();
    bounded(clock.update(enormous, enormous, 48000, 1, 1, false), enormous);
    bounded(clock.update(enormous, enormous, 48000, 1, 1.0e308, false), enormous);
}
}

int main() {
    steadyBlocks();
    jitterAndBursts();
    stopsAndResumes();
    freezeAndDiscontinuities();
    invalidInputs();
    std::cout << "Waveform clock tests passed (" << checks << " checks)\n";
}
