#include "PluginProcessor.h"
#include "WaveformMusic.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <iostream>
#include <numeric>
#include <vector>

// Developer-only native waveform/UI host. No audio device is opened and the
// processor worker is never started: this harness owns the analyzer on the GUI
// thread. PCM is synthetic, deterministic, and never sent to an output device.
namespace {
using namespace juce;
constexpr double sampleRate = 48000.0;
constexpr int blockSize = 800;
constexpr std::uint64_t prefillSamples = 24 * 48000;
const File outputDirectory("/tmp/aurora-waveform-qa");
const File motionDirectory("/tmp/aurora-waveform-motion");
constexpr double motionWarmupSeconds = .35;
constexpr double motionDurationSeconds = 4.0;
constexpr int motionFramesPerSecond = 30;

class WaveformTestApp final : public JUCEApplication, private Timer {
public:
    const String getApplicationName() override { return "Aurora Waveform Tests"; }
    const String getApplicationVersion() override { return "0.1.0"; }

    void initialise(const String& commandLine) override {
        const auto arguments = StringArray::fromTokens(commandLine, true);
        musicFixture = arguments.contains("--music");
        impulseFixture = arguments.contains("--impulses") && !musicFixture;
        clipMode = arguments.contains("--clip");
        const auto destination = clipMode ? motionDirectory : outputDirectory;
        if (!destination.createDirectory().wasOk()) {
            std::cerr << "Cannot create " << destination.getFullPathName() << "\n";
            setApplicationReturnValue(1);
            quit();
            return;
        }
        if (clipMode)
            for (const auto& file : motionDirectory.findChildFiles(File::findFiles, false, "frame-??????.png"))
                file.deleteFile();
        processor = std::make_unique<AuroraAudioProcessor>();
        processor->layoutMode.store(2);
        processor->waveformWindow.store(1);
        processor->waveformColour.store(0);
        processor->isReceiving.store(true);
        const double fillStart = Time::getMillisecondCounterHiRes();
        deliverThrough(prefillSamples);
        prefillMilliseconds = Time::getMillisecondCounterHiRes() - fillStart;
        const auto filled = processor->analyzer.waveformSnapshot();
        check("Prefilled 24 seconds of stereo PCM", filled.totalSamples == prefillSamples
              && !filled.columns.empty() && filled.channelCount == 2);

        window = std::make_unique<Window>();
        editor = processor->createEditor();
        window->setContentOwned(editor, true);
        window->setContentComponentSize(1280, 570);
        window->centreAroundComponent(nullptr, window->getWidth(), window->getHeight());
        window->setVisible(true);
        window->toFront(true);
        started = previousTick = Time::getMillisecondCounterHiRes() * 0.001;
        if (clipMode) motionImages.reserve(motionFramesPerSecond * int(motionDurationSeconds));
        startTimerHz(60);
    }

    void shutdown() override {
        stopTimer();
        window.reset();
        editor = nullptr;
        processor.reset();
    }
    void systemRequestedQuit() override {
        if (!finished)
            finish("Window closed before the scheduled captures completed");
        else
            quit();
    }

private:
    struct Window final : DocumentWindow {
        Window() : DocumentWindow("Aurora Waveform Tests", Colour(0xff101919), allButtons) {
            setUsingNativeTitleBar(true);
            setResizable(true, true);
        }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    std::unique_ptr<AuroraAudioProcessor> processor;
    std::unique_ptr<Window> window;
    AudioProcessorEditor* editor = nullptr;
    std::array<float, blockSize> left{}, right{};
    std::uint64_t sample = 0, frozenHash = 0, stoppedHash = 0;
    std::uint64_t lastPlotHash = 0, silkPlotHash = 0, prismPlotHash = 0;
    std::uint64_t prismFrozenHash = 0, traceFrozenHash = 0;
    std::uint64_t lastExpectedSample = prefillSamples, maximumDeliveryError = 0, stoppedSample = 0;
    double started = 0, previousTick = 0, prefillMilliseconds = 0;
    double lastDeliverySeconds = 0, stoppedAtSeconds = -1;
    double galleryReadyAt = 0;
    std::vector<double> tickIntervals, steadyTickIntervals, feedTimes, captureTimes;
    std::vector<double> motionTimestamps;
    std::vector<Image> motionImages;
    Array<var> motionRecords;
    Array<var> captures, checks;
    int stage = 0, failures = 0, nextMotionSlot = 0;
    bool feeding = true, finished = false, impulseFixture = false, clipMode = false, musicFixture = false;
    bool previousTickCapturedOrConfigured = false;

    void check(const String& name, bool passed) {
        auto* record = new DynamicObject;
        record->setProperty("name", name);
        record->setProperty("passed", passed);
        checks.add(var(record));
        if (!passed) ++failures;
        std::cout << (passed ? "PASS " : "FAIL ") << name.toStdString() << std::endl;
    }

    void feedPCM(int count) {
        jassert(count > 0 && count <= blockSize);
        constexpr double tau = MathConstants<double>::twoPi;
        for (int i = 0; i < count; ++i, ++sample) {
            if (musicFixture) {
                const auto stereo = waveform_test_music::sample(sample, sampleRate);
                left[static_cast<std::size_t>(i)] = stereo.left;
                right[static_cast<std::size_t>(i)] = stereo.right;
                continue;
            }
            const double t = static_cast<double>(sample) / sampleRate;
            const double phrase = std::fmod(t, 24.0);
            const int section = static_cast<int>(phrase / 3.0);
            const double beat = std::fmod(t, 0.5);
            const double attack = std::exp(-beat * 27.0);
            const double kick = .49 * attack * std::sin(tau * (52.0 * t - .24 * std::exp(-beat * 45.0)));
            const double low = .20 * std::sin(tau * 82.41 * t);
            const double middle = .17 * std::sin(tau * 659.25 * t);
            const double high = .14 * std::sin(tau * 8200.0 * t);
            const double swell = .35 + .65 * (.5 + .5 * std::sin(tau * t / 2.7));
            double l = 0, r = 0;
            switch (section) {
                case 0: l = low + kick; r = .42 * low + .75 * kick; break;
                case 1: l = middle * swell; r = .22 * std::sin(tau * 987.77 * t + .7); break;
                case 2: l = high * swell; r = .24 * std::sin(tau * 6400.0 * t) * attack; break;
                case 3: l = kick + low + .48 * middle; r = .7 * kick + .65 * low - .55 * middle; break;
                case 4: l = .48 * std::sin(tau * 220.0 * t) * attack; r = .05 * high; break;
                case 5: l = .015 * low; r = .54 * std::sin(tau * 330.0 * t) * attack; break;
                case 6: // A real PCM gap, with a small event at the end of the section.
                    if (phrase > 20.6) { l = .6 * middle; r = -.7 * middle; }
                    break;
                default: l = (low + middle + high) * swell + .7 * kick;
                         r = (.8 * low - .5 * middle + .65 * high) * swell + .5 * kick;
                         break;
            }
            // Keep deliberate one-sample overloads out of the ordinary musical
            // preview. --impulses enables this separate extrema/clip fixture.
            if (impulseFixture) {
                if (sample % (7 * 48000) == 9600) l = 1.08;
                if (sample % (11 * 48000) == 19200) r = -1.04;
            }
            left[static_cast<std::size_t>(i)] = static_cast<float>(l);
            right[static_cast<std::size_t>(i)] = static_cast<float>(r);
        }
        processor->analyzer.process(left.data(), right.data(), count, sampleRate, 2);
    }

    String fixtureName() const {
        return musicFixture ? "original procedural music" : impulseFixture ? "synthetic tones with impulses" : "synthetic analysis tones";
    }

    String sourceProvenance() const {
        if (musicFixture)
            return "Original deterministic procedural music: eight 4/4 bars at 80 BPM, with pitch-swept kick, snare, hats, syncopated bass, detuned stereo chords, arpeggio and a silent breakdown. Smoothly bounded below 0.92 absolute amplitude; no deliberate clipping or isolated impulses. No recording, external composition or live input; no audio output.";
        return impulseFixture
            ? "Deterministic synthetic stereo tones, decaying kicks and silence, with intentional +1.08 left / -1.04 right single-sample overloads enabled by --impulses. No recorded music or live input."
            : "Deterministic synthetic stereo tones, decaying kicks and silence. Intentional single-sample overloads are disabled. No recorded music or live input.";
    }

    void deliverThrough(std::uint64_t target) {
        // A GUI callback is not an audio clock. Deliver every elapsed sample,
        // including a final partial block, so late paints never underfeed the
        // nominal 48 kHz stream or starve its buffered display clock.
        while (sample < target)
            feedPCM(static_cast<int>(std::min<std::uint64_t>(blockSize, target - sample)));
    }

    void configure(int layout, int width, int height, int timeWindow, int colour) {
        previousTickCapturedOrConfigured = true;
        processor->layoutMode.store(layout);
        processor->waveformWindow.store(timeWindow);
        processor->waveformColour.store(colour);
        window->setContentComponentSize(width, height);
        editor->resized();
        editor->repaint();
    }

    void configureGallery(int timeWindow, int style = 0, int zoom = 0,
                          int colour = 0, int theme = auroraDefaultTheme,
                          int palette = auroraDefaultPalette) {
        // The live-fixture and stopped-tail checks finish before this gallery.
        // All styles therefore receive identical source samples and playhead.
        processor->frozen.store(true);
        processor->waveformStyle.store(style);
        processor->waveformZoom.store(zoom);
        processor->themeMode.store(theme);
        processor->palette.store(palette);
        configure(2, 1280, 570, timeWindow, colour);
        settleGallery();
    }

    void settleGallery() {
        // Use real elapsed time after each mutation or PNG write, rather than
        // deadlines relative to startup that a slow snapshot could skip over.
        galleryReadyAt = Time::getMillisecondCounterHiRes() * .001 + .22;
    }

    static std::uint64_t pixelHash(const Image& image) {
        std::uint64_t hash = 1469598103934665603ull;
        Image::BitmapData data(image, Image::BitmapData::readOnly);
        for (int y = 0; y < data.height; ++y)
            for (int x = 0; x < data.width; ++x) {
                hash ^= data.getPixelColour(x, y).getARGB();
                hash *= 1099511628211ull;
            }
        return hash;
    }

    void captureMotionFrame(double elapsed) {
        if (elapsed < motionWarmupSeconds) return;
        const int slot = static_cast<int>(std::floor((elapsed - motionWarmupSeconds) * motionFramesPerSecond));
        if (slot < nextMotionSlot || slot >= motionFramesPerSecond * int(motionDurationSeconds)) return;
        nextMotionSlot = slot + 1; // Late frames skip slots; never manufacture catch-up frames.
        previousTickCapturedOrConfigured = true;
        const Rectangle<float> area(16, 112, float(editor->getWidth() - 32), float(editor->getHeight() - 128));
        const auto crop = Rectangle<float>(area.getX(), area.getY(), area.getWidth() * .83f - 8, area.getHeight())
            .toNearestInt().getIntersection(editor->getLocalBounds());
        const double began = Time::getMillisecondCounterHiRes() * .001;
        auto image = editor->createComponentSnapshot(crop, true, 1.0f);
        const double snapshotMilliseconds = Time::getMillisecondCounterHiRes() - began * 1000;
        captureTimes.push_back(snapshotMilliseconds);
        motionTimestamps.push_back(began - started - motionWarmupSeconds);
        motionImages.push_back(std::move(image));
        auto* record = new DynamicObject;
        record->setProperty("frame", static_cast<int>(motionImages.size()) - 1);
        record->setProperty("nominalSlot", slot);
        record->setProperty("nominalTimestampSeconds", double(slot) / motionFramesPerSecond);
        record->setProperty("actualTimestampSeconds", motionTimestamps.back());
        record->setProperty("lateMilliseconds", (motionTimestamps.back() - double(slot) / motionFramesPerSecond) * 1000);
        record->setProperty("snapshotMilliseconds", snapshotMilliseconds);
        record->setProperty("cropInEditor", crop.toString());
        record->setProperty("totalSamples", static_cast<int64>(sample));
        record->setProperty("lastDeliveryElapsedSeconds", lastDeliverySeconds);
        motionRecords.add(var(record));
    }

    std::uint64_t capture(const String& name) {
        previousTickCapturedOrConfigured = true;
        const double began = Time::getMillisecondCounterHiRes();
        const auto image = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        const double milliseconds = Time::getMillisecondCounterHiRes() - began;
        captureTimes.push_back(milliseconds);
        const auto file = outputDirectory.getChildFile(name + ".png");
        file.deleteFile();
        bool written = false;
        if (auto stream = file.createOutputStream()) {
            written = PNGImageFormat().writeImageToStream(image, *stream);
            stream->flush();
        }
        check(name + " PNG saved", written && image.isValid());
        auto* record = new DynamicObject;
        record->setProperty("name", name);
        record->setProperty("file", file.getFullPathName());
        record->setProperty("width", image.getWidth());
        record->setProperty("height", image.getHeight());
        record->setProperty("snapshotMilliseconds", milliseconds);
        record->setProperty("layout", processor->layoutMode.load());
        record->setProperty("waveformWindow", processor->waveformWindow.load());
        record->setProperty("waveformColour", processor->waveformColour.load());
        record->setProperty("waveformStyle", processor->waveformStyle.load());
        record->setProperty("waveformZoom", processor->waveformZoom.load());
        record->setProperty("displayGain", 1 << processor->waveformZoom.load());
        record->setProperty("expectedAmplitudeAxisLimit", 1.0 / (1 << processor->waveformZoom.load()));
        record->setProperty("theme", processor->themeMode.load());
        record->setProperty("palette", processor->palette.load());
        record->setProperty("customColourA", String::toHexString(processor->customColourA.load()));
        record->setProperty("customColourB", String::toHexString(processor->customColourB.load()));
        record->setProperty("customColourC", String::toHexString(processor->customColourC.load()));
        record->setProperty("frozen", processor->frozen.load());
        record->setProperty("totalSamples", static_cast<int64>(sample));
        record->setProperty("expectedSamplesAtLastDelivery", static_cast<int64>(lastExpectedSample));
        record->setProperty("lastDeliveryElapsedSeconds", lastDeliverySeconds);
        record->setProperty("sampleDeliveryError", static_cast<int64>(sample) - static_cast<int64>(lastExpectedSample));
        record->setProperty("impulseFixture", impulseFixture);
        record->setProperty("sourceFixture", fixtureName());
        record->setProperty("elapsedSeconds", Time::getMillisecondCounterHiRes() * .001 - started);
        Array<var> controls;
        bool contained = true;
        for (int i = 0; i < editor->getNumChildComponents(); ++i) {
            auto* child = editor->getChildComponent(i);
            if (!child->isVisible()) continue;
            auto* control = new DynamicObject;
            control->setProperty("title", child->getTitle());
            control->setProperty("name", child->getName());
            control->setProperty("bounds", child->getBounds().toString());
            const bool inside = !child->getBounds().isEmpty()
                && editor->getLocalBounds().contains(child->getBounds());
            control->setProperty("insideEditor", inside);
            contained &= inside;
            controls.add(var(control));
        }
        record->setProperty("visibleControls", var(controls));
        check(name + " visible controls fit inside the editor", contained);
        const auto hash = pixelHash(image);
        record->setProperty("pixelHash", String::toHexString(static_cast<int64>(hash)));
        if (processor->layoutMode.load() == 2) {
            // An interior rectangle excludes selectors, panel title, labels,
            // axes and the levels panel. Different selector text alone cannot
            // satisfy the style-change assertions below.
            const Rectangle<int> plot(48, 170,
                jmax(1, int((image.getWidth() - 32) * .83) - 76),
                jmax(1, image.getHeight() - 240));
            const auto clippedPlot = plot.getIntersection(image.getBounds());
            lastPlotHash = pixelHash(image.getClippedImage(clippedPlot));
            record->setProperty("waveformInteriorBounds", clippedPlot.toString());
            record->setProperty("waveformInteriorHash", String::toHexString(static_cast<int64>(lastPlotHash)));
        }
        captures.add(var(record));
        return hash;
    }

    void timerCallback() override {
        const double now = Time::getMillisecondCounterHiRes() * .001;
        const double elapsed = now - started;
        tickIntervals.push_back((now - previousTick) * 1000.0);
        if (!previousTickCapturedOrConfigured && elapsed > .4)
            steadyTickIntervals.push_back((now - previousTick) * 1000.0);
        previousTickCapturedOrConfigured = false;
        previousTick = now;
        if (feeding) {
            const double before = Time::getMillisecondCounterHiRes();
            lastDeliverySeconds = std::max(0.0, elapsed);
            lastExpectedSample = prefillSamples
                + static_cast<std::uint64_t>(std::floor(lastDeliverySeconds * sampleRate));
            deliverThrough(lastExpectedSample);
            maximumDeliveryError = std::max(maximumDeliveryError,
                sample > lastExpectedSample ? sample - lastExpectedSample : lastExpectedSample - sample);
            feedTimes.push_back(Time::getMillisecondCounterHiRes() - before);
        }
        if (clipMode) {
            if (elapsed >= motionWarmupSeconds + motionDurationSeconds) {
                feeding = false; stoppedAtSeconds = elapsed; stoppedSample = sample;
                processor->isReceiving.store(false);
                finish();
            } else {
                captureMotionFrame(elapsed);
            }
            return;
        }
        // Every configuration gets several message-loop frames before capture.
        switch (stage) {
            case 0: if (elapsed >= .60) { capture("waveform-12s-bands"); ++stage; } break;
            case 1: if (elapsed >= .95) { configure(2,1280,570,0,1); ++stage; } break;
            case 2: if (elapsed >= 1.30) { capture("waveform-6s-palette"); ++stage; } break;
            case 3: if (elapsed >= 1.60) { configure(2,1280,570,2,0); ++stage; } break;
            case 4: if (elapsed >= 1.95) { capture("waveform-24s-bands"); ++stage; } break;
            case 5: if (elapsed >= 2.20) { processor->frozen.store(true); ++stage; } break;
            case 6: if (elapsed >= 2.45) { frozenHash = capture("waveform-frozen-a"); ++stage; } break;
            case 7: if (elapsed >= 2.80) {
                check("Freeze holds identical rendered pixels while PCM continues",
                      capture("waveform-frozen-b") == frozenHash);
                processor->frozen.store(false); ++stage;
            } break;
            case 8: if (elapsed >= 3.20) {
                check("Resume changes the rendered waveform", capture("waveform-resumed") != frozenHash);
                configure(2,1280,570,3,0); ++stage;
            } break;
            case 9: if (elapsed >= 3.65) { capture("waveform-scope"); configure(0,1100,620,1,0); ++stage; } break;
            case 10: if (elapsed >= 4.40) { capture("studio-minimum"); configure(1,1100,390,1,0); ++stage; } break;
            case 11: if (elapsed >= 5.00) { capture("strip-minimum"); configure(2,1100,390,0,1); ++stage; } break;
            case 12: if (elapsed >= 5.60) { capture("waveform-compact"); ++stage; } break;
            case 13: if (elapsed >= 6.50) {
                feeding = false; stoppedAtSeconds = elapsed; stoppedSample = sample;
                processor->isReceiving.store(false); ++stage;
            } break;
            case 14: if (elapsed >= 7.10) { stoppedHash = capture("waveform-stopped-a"); ++stage; } break;
            case 15: if (elapsed >= 7.60) {
                check("Stopped input holds identical rendered pixels after its real tail",
                      capture("waveform-stopped-b") == stoppedHash);
                ++stage;
            } break;
            case 16: if (elapsed >= 7.90) { configureGallery(4); ++stage; } break;
            case 17: if (now >= galleryReadyAt) {
                capture("gallery-silk-3s-bands-1x");
                configureGallery(5); ++stage;
            } break;
            case 18: if (now >= galleryReadyAt) {
                capture("gallery-silk-1s-bands-1x"); silkPlotHash = lastPlotHash;
                configureGallery(5, 1); ++stage;
            } break;
            case 19: if (now >= galleryReadyAt) {
                prismFrozenHash = capture("gallery-prism-1s-bands-1x-a");
                prismPlotHash = lastPlotHash;
                check("Prism changes the waveform interior on identical frozen PCM", prismPlotHash != silkPlotHash);
                settleGallery(); ++stage;
            } break;
            case 20: if (now >= galleryReadyAt) {
                check("Prism stays pixel-identical while frozen",
                      capture("gallery-prism-1s-bands-1x-b") == prismFrozenHash);
                configureGallery(5, 2); ++stage;
            } break;
            case 21: if (now >= galleryReadyAt) {
                traceFrozenHash = capture("gallery-trace-1s-bands-1x-a");
                check("Trace differs from Silk and Prism on identical frozen PCM",
                      lastPlotHash != silkPlotHash && lastPlotHash != prismPlotHash);
                settleGallery(); ++stage;
            } break;
            case 22: if (now >= galleryReadyAt) {
                check("Trace stays pixel-identical while frozen",
                      capture("gallery-trace-1s-bands-1x-b") == traceFrozenHash);
                configureGallery(5); ++stage;
            } break;
            case 23: if (now >= galleryReadyAt) {
                capture("gallery-silk-1s-restored");
                check("Returning to Silk restores the exact waveform interior", lastPlotHash == silkPlotHash);
                configureGallery(4, 0, 1); ++stage;
            } break;
            case 24: if (now >= galleryReadyAt) {
                capture("gallery-silk-3s-2x-axis-half");
                configureGallery(4, 0, 2); ++stage;
            } break;
            case 25: if (now >= galleryReadyAt) {
                capture("gallery-silk-3s-4x-axis-quarter");
                configureGallery(4, 0, 0, 1, 2, 9); ++stage;
            } break;
            case 26: if (now >= galleryReadyAt) {
                capture("gallery-chrome-ion-blue-3s-palette");
                configureGallery(4, 0, 0, 1, 1, 12); ++stage;
            } break;
            case 27: if (now >= galleryReadyAt) {
                capture("gallery-afterhours-tape-violet-3s-palette");
                processor->customColourA.store(0x153c44u);
                processor->customColourB.store(0x55c7c0u);
                processor->customColourC.store(0xf3a8bcu);
                configureGallery(4, 0, 0, 1, auroraDefaultTheme, auroraCustomPalette); ++stage;
            } break;
            case 28: if (now >= galleryReadyAt) {
                capture("gallery-custom-teal-rose-3s-palette");
                configureGallery(1); ++stage;
            } break;
            case 29: if (now >= galleryReadyAt) {
                capture("gallery-default-silk-12s-bands-1x");
                check("Gallery changes presentation without delivering additional PCM",
                      sample == stoppedSample && processor->analyzer.waveformSnapshot().totalSamples == stoppedSample);
                ++stage;
            } break;
            default: finish(); break;
        }
    }

    static var timings(const std::vector<double>& values) {
        auto* stats = new DynamicObject;
        stats->setProperty("count", static_cast<int>(values.size()));
        if (!values.empty()) {
            auto sorted = values;
            std::sort(sorted.begin(), sorted.end());
            stats->setProperty("meanMilliseconds", std::accumulate(values.begin(),values.end(),0.0)/values.size());
            stats->setProperty("p95Milliseconds", sorted[std::min(sorted.size()-1,
                static_cast<std::size_t>(std::ceil(sorted.size()*.95)-1))]);
            stats->setProperty("maxMilliseconds", sorted.back());
        }
        return var(stats);
    }

    void finishMotion(const String& error) {
        if (finished) return;
        finished = true;
        stopTimer();
        feeding = false;
        if (error.isNotEmpty()) check(error, false);
        const auto analyzedSamples = processor->analyzer.waveformSnapshot().totalSamples;
        check("Motion capture preserves exact wall-clock PCM delivery",
              maximumDeliveryError == 0 && analyzedSamples == sample && sample == lastExpectedSample);
        check("Motion capture contains changing native waveform pixels", motionImages.size() > 1
              && pixelHash(motionImages.front()) != pixelHash(motionImages.back()));
        const auto fullEditor = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.0f);
        std::vector<double> intervals, pngTimes;
        String manifest("ffconcat version 1.0\n"), lastFile;
        int savedFrames = 0;
        // Keep PNG encoding off the capture timeline. At most 120 cropped
        // native images are retained; this is bounded developer-only memory.
        for (std::size_t i = 0; i < motionImages.size(); ++i) {
            const auto name = "frame-" + String(static_cast<int>(i)).paddedLeft('0', 6) + ".png";
            const auto file = motionDirectory.getChildFile(name);
            file.deleteFile();
            const double began = Time::getMillisecondCounterHiRes();
            bool written = false;
            if (auto stream = file.createOutputStream()) {
                written = PNGImageFormat().writeImageToStream(motionImages[i], *stream);
                stream->flush();
            }
            pngTimes.push_back(Time::getMillisecondCounterHiRes() - began);
            if (written) ++savedFrames;
            auto* record = motionRecords.getReference(static_cast<int>(i)).getDynamicObject();
            record->setProperty("file", file.getFullPathName());
            record->setProperty("saved", written);
            record->setProperty("pngWriteMilliseconds", pngTimes.back());
            record->setProperty("pixelHash", String::toHexString(static_cast<int64>(pixelHash(motionImages[i]))));
            if (i > 0) intervals.push_back((motionTimestamps[i] - motionTimestamps[i - 1]) * 1000);
            const double duration = i + 1 < motionTimestamps.size()
                ? motionTimestamps[i + 1] - motionTimestamps[i]
                : std::max(.001, motionDurationSeconds - motionTimestamps[i]);
            manifest << "file '" << name << "'\nduration " << String(duration, 9) << "\n";
            lastFile = name;
        }
        if (lastFile.isNotEmpty()) manifest << "file '" << lastFile << "'\n";
        check("Every captured motion frame was saved", savedFrames == static_cast<int>(motionImages.size()));
        check("Timestamp-aware encoding manifest saved", motionDirectory.getChildFile("frames.ffconcat").replaceWithText(manifest));
        const auto fullEditorFile = motionDirectory.getChildFile("full-editor.png");
        fullEditorFile.deleteFile();
        bool fullEditorWritten = false;
        if (auto stream = fullEditorFile.createOutputStream()) {
            fullEditorWritten = PNGImageFormat().writeImageToStream(fullEditor, *stream);
            stream->flush();
        }
        check("Full native editor screenshot saved", fullEditorWritten && fullEditor.isValid());
        auto* report = new DynamicObject;
        report->setProperty("passed", failures == 0);
        report->setProperty("failures", failures);
        report->setProperty("checks", var(checks));
        report->setProperty("frames", var(motionRecords));
        report->setProperty("frameCount", static_cast<int>(motionImages.size()));
        report->setProperty("requestedFramesPerSecond", motionFramesPerSecond);
        report->setProperty("requestedDurationSeconds", motionDurationSeconds);
        report->setProperty("warmupSeconds", motionWarmupSeconds);
        report->setProperty("missedNominalFrameSlots", motionFramesPerSecond * int(motionDurationSeconds) - static_cast<int>(motionImages.size()));
        report->setProperty("actualFrameIntervals", timings(intervals));
        report->setProperty("componentSnapshotTiming", timings(captureTimes));
        report->setProperty("pngWriteTimingAfterCapture", timings(pngTimes));
        report->setProperty("analysisFeedTiming", timings(feedTimes));
        report->setProperty("timerIntervals", timings(tickIntervals));
        report->setProperty("audioDeviceOpened", false);
        report->setProperty("audioSampleRate", sampleRate);
        report->setProperty("analyzedSamples", static_cast<int64>(analyzedSamples));
        report->setProperty("maximumSampleDeliveryError", static_cast<int64>(maximumDeliveryError));
        report->setProperty("waveformWindowSeconds", 12);
        report->setProperty("waveformStyle", "Silk");
        report->setProperty("colourMode", "Bands");
        report->setProperty("impulseFixture", impulseFixture);
        report->setProperty("sourceFixture", fixtureName());
        report->setProperty("sourceProvenance", sourceProvenance());
        report->setProperty("fullEditorScreenshot", fullEditorFile.getFullPathName());
        report->setProperty("captureMethod", "Real native JUCE component snapshots cropped to the full waveform panel, including its controls and axes, at 1x logical size. Up to 120 images are buffered and PNG-encoded only after recording stops.");
        report->setProperty("encoding", "frames.ffconcat uses actual capture intervals, with the final file repeated to apply its duration. Numbered PNGs alone imply 30 fps and may distort timing if missedNominalFrameSlots is nonzero. No frames are synthesized.");
        report->setProperty("timingLimit", "Native GUI-thread QA capture; snapshot work can affect timer and paint timing. This is not a real-time DAW performance benchmark.");
        if (!motionDirectory.getChildFile("report.json").replaceWithText(JSON::toString(var(report), true))) ++failures;
        std::cout << "Waveform motion capture: " << savedFrames << " frames, " << failures
                  << " failures; /tmp/aurora-waveform-motion" << std::endl;
        setApplicationReturnValue(failures == 0 ? 0 : 1);
        quit();
    }

    void finish(const String& error = {}) {
        if (clipMode) { finishMotion(error); return; }
        if (finished) return;
        finished = true;
        stopTimer(); // No further PCM, captures, or quit requests during teardown.
        if (error.isNotEmpty()) check(error, false);
        const auto analyzedSamples = processor->analyzer.waveformSnapshot().totalSamples;
        check("Wall-clock PCM delivery and analyzer sample clock match exactly",
              maximumDeliveryError == 0 && analyzedSamples == sample);
        check("Musical fixture advances at 48 kHz until input is stopped",
              stoppedAtSeconds >= 0 && stoppedSample == prefillSamples
                  + static_cast<std::uint64_t>(std::floor(stoppedAtSeconds * sampleRate))
                  && sample == stoppedSample);
        auto* report = new DynamicObject;
        report->setProperty("passed", failures == 0);
        report->setProperty("failures", failures);
        report->setProperty("checks", var(checks));
        report->setProperty("captures", var(captures));
        report->setProperty("audioDeviceOpened", false);
        report->setProperty("audioSampleRate", sampleRate);
        report->setProperty("inputBlockSamples", blockSize);
        report->setProperty("finalPartialBlocks", true);
        report->setProperty("impulseFixture", impulseFixture);
        report->setProperty("sourceFixture", fixtureName());
        report->setProperty("sourceProvenance", sourceProvenance());
        report->setProperty("prefillSeconds", 24);
        report->setProperty("prefillMilliseconds", prefillMilliseconds);
        report->setProperty("timerHzRequested", 60);
        report->setProperty("timerIntervals", timings(tickIntervals));
        report->setProperty("steadyTimerIntervals", timings(steadyTickIntervals));
        report->setProperty("steadyTimerIntervalLimit", "Excludes startup through 0.4 s and the interval immediately following a capture or layout change; still a GUI-thread fixture, not DAW performance.");
        report->setProperty("analysisFeedTiming", timings(feedTimes));
        report->setProperty("componentSnapshotTiming", timings(captureTimes));
        report->setProperty("captureMethod", "Real JUCE editor component snapshots at 1x logical size; validates raster waveform and UI. The Studio OpenGL child is not captured by this method.");
        report->setProperty("styleComparison", "After input stops, frozen Silk/Prism/Trace use identical samples, window, dimensions, palette and gain. Interior hashes exclude controls, titles, axes and levels. Repeated Prism/Trace captures must match, and returning to Silk must restore its interior exactly.");
        report->setProperty("axisVerification", "Zoom screenshots include the painted amplitude axis; expectedAmplitudeAxisLimit records 1, 0.5 or 0.25 for visual review. No OCR or numeric inference from pixels is performed.");
        report->setProperty("timingLimit", "Developer GUI-thread feed and PNG captures affect timer intervals; these are QA observations, not real-time audio or DAW performance measurements.");
        report->setProperty("deliveryClock", "Prefill + floor(monotonic elapsed seconds * 48000), completed exactly in blocks of at most 800 samples until the explicit input stop.");
        report->setProperty("maximumSampleDeliveryError", static_cast<int64>(maximumDeliveryError));
        report->setProperty("lastExpectedSample", static_cast<int64>(lastExpectedSample));
        report->setProperty("deliveredSamples", static_cast<int64>(sample));
        report->setProperty("analyzedSamples", static_cast<int64>(analyzedSamples));
        report->setProperty("stoppedAtElapsedSeconds", stoppedAtSeconds);
        if (!outputDirectory.getChildFile("report.json").replaceWithText(JSON::toString(var(report), true))) {
            ++failures;
            std::cerr << "FAIL writing waveform report\n";
        }
        std::cout << "Waveform rendering QA: " << failures << " failures; "
                  << captures.size() << " captures in /tmp/aurora-waveform-qa" << std::endl;
        setApplicationReturnValue(failures == 0 ? 0 : 1);
        quit();
    }
};
}
START_JUCE_APPLICATION(WaveformTestApp)
