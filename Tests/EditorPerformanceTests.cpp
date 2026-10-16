#include "PluginEditor.h"
#include "SpectralProjection.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
int checks = 0;
juce::Array<juce::var> results;
void require(bool condition, const juce::String& description) {
    ++checks;
    if (!condition) throw std::runtime_error(description.toStdString());
}
float unit(float x) { return juce::jlimit(0.f, 1.f, x); }
juce::Colour rgb(std::uint32_t x) { return juce::Colour(0xff000000u | x); }
bool samePixels(const juce::Image& a, const juce::Image& b) {
    if (a.getBounds() != b.getBounds() || a.getFormat() != b.getFormat()) return false;
    juce::Image::BitmapData x(a, juce::Image::BitmapData::readOnly), y(b, juce::Image::BitmapData::readOnly);
    if (x.pixelStride != y.pixelStride) return false;
    for (int row = 0; row < a.getHeight(); ++row)
        if (std::memcmp(x.getLinePointer(row), y.getLinePointer(row),
                        std::size_t(a.getWidth() * x.pixelStride)) != 0) return false;
    return true;
}
struct Timing {
    double median = 0, maximum = 0;
};
Timing measure(const std::function<void()>& operation, int repetitions, int iterations = 1) {
    std::vector<double> values;
    for (int run = 0; run < repetitions; ++run) {
        const auto begin = juce::Time::getMillisecondCounterHiRes();
        for (int iteration = 0; iteration < iterations; ++iteration) operation();
        values.push_back((juce::Time::getMillisecondCounterHiRes() - begin) / iterations);
    }
    std::sort(values.begin(), values.end());
    return {values[values.size() / 2], values.back()};
}
void record(const juce::String& name, Timing before, Timing after, const juce::String& definition) {
    auto* row = new juce::DynamicObject;
    row->setProperty("name", name); row->setProperty("definition", definition);
    row->setProperty("baselineMedianMs", before.median); row->setProperty("optimizedMedianMs", after.median);
    row->setProperty("baselineMaximumMs", before.maximum); row->setProperty("optimizedMaximumMs", after.maximum);
    row->setProperty("medianSpeedup", after.median > 0 ? before.median / after.median : 0);
    results.add(juce::var(row));
    std::cout << name << ": " << before.median << " ms -> " << after.median << " ms\n";
}
} // namespace

// Deliberately narrow friend probe. The production editor stays unshown, its
// message timer is stopped, and no audio device or native GL surface is opened.
struct EditorPerformanceProbe {
    using Editor = AuroraAudioProcessorEditor;
    static void stop(Editor& editor) { editor.stopTimer(); }
    static void unavailableGraphicsTests() {
        // No window/context or driver functions are loaded here. Exercising
        // both callbacks catches accidental GL calls in the unsupported path,
        // including cleanup after Windows returns a legacy context.
        VisualSurface surface;
        surface.newOpenGLContextCreated();
        require(surface.failed.load() && !surface.ready.load() && !surface.shader,
                "Unavailable graphics is rejected before creating a shader");
        require(!surface.pearlFlow.isReady() && !surface.postProcess.isReady(),
                "Unavailable graphics creates no fluid or bloom resources");
        surface.openGLContextClosing();
        require(!surface.ready.load() && !surface.shader,
                "Rejected graphics context cleans up without driver entry points");
    }
    static void expandedSceneTests(Editor& e) {
        const auto original=e.processor.createAppearancePreset();
        e.processor.frozen.store(true);
        e.processor.cameraYaw.store(.4f);
        e.processor.custom.set("layoutColumns",1);
        e.processor.custom.set("showScene",0);
        for(int layout=0;layout<4;++layout) {
            e.processor.layoutMode.store(layout);e.syncControls();
            e.setSize(1280,layout==1?390:layout==2?570:820);
            if(layout==3){e.dashboardOffset=150;e.resized();}
            const auto appearance=e.processor.createAppearancePreset();
            const auto modules=e.moduleAreas();const auto size=e.getBounds();
            const auto offset=e.dashboardOffset;
            e.setSceneExpanded(true);
            require(e.processor.sceneExpanded.load(),"Expand must commit the presentation flag");
            require(e.moduleAreas()[0]==e.contentArea(),"Expanded scene must occupy all available content");
            for(std::size_t i=1;i<6;++i)require(e.moduleAreas()[i].isEmpty(),"Expanded scene must hide every meter module");
            require(e.sceneClip.getBounds()==juce::Rectangle<int>(0,40,e.getWidth(),e.getHeight()-40),"Expanded viewport must fill the editor below the restore bar");
            require(e.visualSurface.getBounds()==e.sceneClip.getLocalBounds(),"Expanded GPU surface must fill its viewport without dashboard insets");
            require(e.expandScene.isVisible() && e.expandScene.getToggleState(),"Restore control must remain accessible");
            require(!e.layout.isVisible() && !e.customise.isVisible() && !e.waveWindowChoice.isVisible()
                    && !e.waveStyleChoice.isVisible() && !e.dashboardScroll.isVisible(),"Expanded mode must hide dashboard controls");
            require(e.processor.createAppearancePreset()==appearance && e.processor.frozen.load()
                    && e.processor.cameraYaw.load()==.4f,"Expansion must preserve appearance, freeze and camera");
            require(e.getBounds()==size,"Expansion must preserve the host-owned window bounds");
            require(e.keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)),"Escape must restore the meters");
            require(!e.processor.sceneExpanded.load() && e.moduleAreas()==modules && e.dashboardOffset==offset,
                    "Restoring must recover the original module bounds and custom scroll");
            require(e.layout.isVisible() && e.customise.isVisible() && !e.expandScene.getToggleState(),"Restoring must reveal dashboard controls");
        }
        e.processor.sceneExpanded.store(true);e.syncControls();
        require(e.expandScene.getToggleState() && !e.layout.isVisible(),"Host-restored expansion must synchronize the live editor");
        e.setSize(3840,2160);
        require(e.visualSurface.getBounds()==juce::Rectangle<int>(0,0,3840,2120),"Expanded content must support a 4K-sized host window");
        e.processor.sceneExpanded.store(false);e.syncControls();
        require(!e.expandScene.getToggleState() && e.layout.isVisible(),"Host-restored collapse must synchronize the live editor");
        e.processor.applyAppearancePreset(original);e.processor.frozen.store(false);e.syncControls();
    }
    static void seedStereo(Editor& editor) {
        editor.stereoTrails.clear(); editor.meterTime = 4.; editor.stereoEnvelope = .72f;
        for (int trace = 0; trace < 64; ++trace) {
            Editor::StereoTrace value; value.time = editor.meterTime - double(trace) / 60.;
            for (int point = 0; point < 1024; ++point) {
                const float t = float(point) * .017f + float(trace) * .023f;
                value.points.emplace_back(.26f * std::sin(t) + .08f * std::sin(t * 4.3f),
                                          .29f * std::cos(t * 1.03f) + .07f * std::cos(t * 3.1f));
            }
            editor.stereoTrails.push_back(std::move(value));
        }
    }
    // The pre-optimization rasterizer is retained only in this test. The
    // brightness registry lookup remains inside the 254x254 pixel loop.
    static void baselineStereo(Editor& e) {
        using namespace juce;
        constexpr int n = Editor::stereoResolution;
        std::fill(e.stereoDensity.begin(), e.stereoDensity.end(), 0.f);
        const float normal = e.processor.custom.get("stereoAutoGain") > .5f ? std::max(.075f, e.stereoEnvelope) : 1.f;
        const float scale = .46f / normal * float(n - 1) * e.processor.custom.get("stereoGain");
        const float trail = e.processor.custom.get("stereoTrail");
        for (const auto& trace : e.stereoTrails) {
            const float age = float(e.meterTime - trace.time);
            const float weight = std::exp(-age / (trail * .31f)) * unit(1.f - age / trail);
            if (weight <= .001f) continue;
            for (const auto& point : trace.points) {
                const float x = float(n - 1) * .5f + point.x * scale, y = float(n - 1) * .5f + point.y * scale;
                if (x < 1 || y < 1 || x >= float(n - 2) || y >= float(n - 2)) continue;
                const int ix = int(x), iy = int(y); const float fx = x - float(ix), fy = y - float(iy);
                e.stereoDensity[std::size_t(iy * n + ix)] += weight * (1 - fx) * (1 - fy);
                e.stereoDensity[std::size_t(iy * n + ix + 1)] += weight * fx * (1 - fy);
                e.stereoDensity[std::size_t((iy + 1) * n + ix)] += weight * (1 - fx) * fy;
                e.stereoDensity[std::size_t((iy + 1) * n + ix + 1)] += weight * fx * fy;
            }
        }
        e.stereoCache.clear(e.stereoCache.getBounds(), Colours::transparentBlack);
        Image::BitmapData pixels(e.stereoCache, Image::BitmapData::writeOnly);
        const bool dark = e.isDark(); const auto colours = e.moduleColours("stereo");
        const auto low = rgb(colours[0]).interpolatedWith(rgb(colours[1]), .65f).interpolatedWith(e.ink, dark ? .06f : .4f);
        const auto high = rgb(colours[2]).interpolatedWith(e.ink, dark ? .04f : .5f);
        for (int y = 1; y < n - 1; ++y) for (int x = 1; x < n - 1; ++x) {
            const std::size_t i = std::size_t(y * n + x);
            const float density = e.stereoDensity[i];
            const float halo = (e.stereoDensity[i - 1] + e.stereoDensity[i + 1] + e.stereoDensity[i - n] + e.stereoDensity[i + n]) * .085f;
            const float alpha = 1.f - std::exp(-(density + halo) * .85f * e.processor.custom.get("stereoBrightness"));
            if (alpha > .004f) pixels.setPixelColour(x, y, low.interpolatedWith(high, unit(density * .32f)).withAlpha(alpha));
        }
    }
    static void stereoTests(Editor& e) {
        seedStereo(e);
        for (int theme = 0; theme < 4; ++theme) for (float brightness : {.2f, 1.f, 3.f}) for (int automatic : {0, 1}) {
            e.processor.themeMode.store(theme); e.processor.custom.set("stereoBrightness", brightness);
            e.processor.custom.set("stereoAutoGain", float(automatic)); e.processor.custom.set("stereoTrail", 2);
            e.processor.custom.set("stereoGain", automatic ? 1.5f : .75f);
            e.processor.custom.set("stereoOwnPalette", float(theme % 2));
            e.processor.custom.set("stereoColourA", float(0x57365b)); e.processor.custom.set("stereoColourB", float(0xdf8a98));
            e.processor.custom.set("stereoColourC", float(0xacf2c8)); e.setPalette();
            baselineStereo(e); const auto reference = e.stereoCache.createCopy();
            const auto density = e.stereoDensity;
            e.rebuildStereo();
            require(samePixels(reference, e.stereoCache), "Stereo brightness hoist must preserve every output pixel across themes, brightness and gain modes");
            require(density == e.stereoDensity, "Stereo optimization must preserve every measured density value");
        }
        // Warm both paths, then report medians. Timing ratios are evidence, not
        // brittle CI assertions tied to CPU speed or competing desktop work.
        for (int i = 0; i < 3; ++i) { baselineStereo(e); e.rebuildStereo(); }
        const auto before = measure([&] { baselineStereo(e); }, 11);
        const auto after = measure([&] { e.rebuildStereo(); }, 11);
        record("stereo-raster", before, after,
               "Same 64 retained traces of 1024 points, 256x256 raster. Baseline repeats the old brightness registry lookup per pixel; optimized path calls production rebuildStereo. Pixels verified byte-identical.");
    }
    static void seedSpectrogram(Editor& e) {
        e.history.clear(); e.frame.sampleRate = 48000.;
        for (int column = 0; column < Editor::historyColumns; ++column) {
            std::vector<float> bins(2049);
            for (std::size_t bin = 0; bin < bins.size(); ++bin) {
                const double x = double(bin) / double(bins.size() - 1);
                const double peak = std::exp(-std::pow((x - .025 - .001 * (column % 37)) / .035, 2.));
                bins[bin] = float(-98. + 68. * peak + 17. * (1. + std::sin(x * 83. + column * .09)));
            }
            e.history.push_back(std::move(bins));
        }
    }
    static void spectralBase(Editor& e, bool own = false, int theme = 3) {
        auto& p = e.processor;
        p.resetAppearance(); p.frozen.store(true); p.themeMode.store(theme); p.palette.store(auroraCustomPalette);
        p.customColourA.store(0x20485c); p.customColourB.store(0x88d1bb); p.customColourC.store(0xf8d4af);
        p.custom.set("interfaceOverride", 1); p.custom.set("panelColour", float(0x152135));
        p.custom.set("spectrogramColourA", float(0x48355c)); p.custom.set("spectrogramColourB", float(0xd898c6));
        p.custom.set("spectrogramColourC", float(0xe7e5b5)); p.custom.set("spectrogramOwnPalette", own ? 1 : 0);
        e.setPalette(); e.spectrogramAppearanceValid = false; e.rebuildSpectrogram();
    }
    // Preserve the original per-row dB-range lookups for a precise comparison
    // with production writeSpectrogramColumn's hoisted range reads.
    static void baselineSpectrogram(Editor& e) {
        using namespace juce;
        e.spectrogramCache = Image(Image::ARGB, Editor::historyColumns, Editor::historyRows, true);
        int column = Editor::historyColumns - int(e.history.size());
        for (const auto& bins : e.history) {
            Image::BitmapData pixels(e.spectrogramCache, column++, 0, 1, Editor::historyRows, Image::BitmapData::writeOnly);
            for (int row = 0; row < Editor::historyRows; ++row) {
                const float x = (float(row) + .5f) / float(Editor::historyRows);
                const float db = spectralPeak(bins, e.frame.sampleRate, x, 1.f / float(Editor::historyRows));
                const float floor = e.processor.custom.get("spectrogramFloor"), ceiling = e.processor.custom.get("spectrogramCeiling");
                pixels.setPixelColour(0, Editor::historyRows - 1 - row,
                    e.spectralLut[std::size_t(jlimit(0, 255, int(std::round(unit((db - floor) / (ceiling - floor)) * 255.f))))]);
            }
        }
    }
    static void spectrogramTests(Editor& e) {
        seedSpectrogram(e); spectralBase(e);
        const auto source = e.history;
        baselineSpectrogram(e); const auto baseline = e.spectrogramCache.createCopy();
        e.spectrogramAppearanceValid = false; e.rebuildSpectrogram();
        require(samePixels(baseline, e.spectrogramCache), "Spectrogram range lookup hoist must preserve the entire retained image byte-for-byte");
        const auto before = measure([&] { baselineSpectrogram(e); }, 5);
        const auto after = measure([&] { e.spectrogramAppearanceValid = false; e.rebuildSpectrogram(); }, 5);
        record("spectrogram-full-rebuild", before, after,
               "480x256 retained spectral image. Baseline uses original per-row range lookups with an existing LUT; production rebuild additionally refreshes the LUT and appearance signature. Pixels verified byte-identical.");
        const auto cachedImage = e.spectrogramCache;
        const auto cached = measure([&] { e.rebuildSpectrogram(); }, 9, 2000);
        require(e.spectrogramCache == cachedImage, "Unchanged spectrogram appearance must reuse the existing pixel buffer");
        record("spectrogram-unchanged-appearance", after, cached,
               "Full production rebuild versus production early-return for unchanged appearance and retained history. Cache identity and output verified; no per-frame history rasterization is needed.");

        const std::array<std::pair<const char*, float>, 9> unrelated {{
            {"waveDensity", 1.8f}, {"layoutColumns", 3}, {"layoutGap", 18}, {"sceneBloom", 2.5f},
            {"sceneSpeed", .4f}, {"spectrumFloor", -66}, {"stereoBrightness", 2}, {"spectrogramGrid", 0},
            {"spectrogramSeconds", 6}
        }};
        for (const auto& value : unrelated) {
            const auto cache = e.spectrogramCache;
            e.processor.custom.set(value.first, value.second); e.customizationChanged();
            require(e.spectrogramCache == cache && samePixels(baseline, e.spectrogramCache),
                    juce::String("Unrelated edit must reuse unchanged spectral pixels: ") + value.first);
        }
        // Disabled local colours are not part of the resolved spectrogram map.
        auto image = e.spectrogramCache;
        e.processor.custom.set("spectrogramColourB", float(0x78bdec)); e.customizationChanged();
        require(e.spectrogramCache == image, "Editing a disabled module palette must not invalidate spectral pixels");

        struct Change { const char* key; float value; bool own; int theme; };
        const std::array<Change, 12> relevant {{
            {"spectrogramFloor", -72, false, 3}, {"spectrogramCeiling", 6, false, 3},
            {"spectrogramContrast", 1.7f, false, 3}, {"paletteMiddle", .72f, false, 3},
            {"paletteReverse", 1, false, 3}, {"spectrogramOwnPalette", 1, false, 3},
            {"spectrogramColourA", float(0x8f4538), true, 3}, {"spectrogramColourB", float(0x55cfea), true, 3},
            {"spectrogramColourC", float(0xa7f2ba), true, 3}, {"panelColour", float(0x432d26), false, 3},
            {"interfaceOverride", 0, false, 3}, {"textColour", float(0xfab267), false, 0}
        }};
        for (const auto& change : relevant) {
            spectralBase(e, change.own, change.theme);
            const auto cache = e.spectrogramCache, pixels = cache.createCopy();
            e.processor.custom.set(change.key, change.value); e.customizationChanged();
            require(e.spectrogramCache != cache && !samePixels(pixels, e.spectrogramCache),
                    juce::String("Relevant edit must recolour existing spectral history: ") + change.key);
            require(e.history == source, "Recolouring must leave every retained spectral measurement unchanged");
        }
        spectralBase(e);
        image = e.spectrogramCache; auto previous = image.createCopy();
        e.processor.customColourB.store(0xdb809a); e.customizationChanged();
        require(e.spectrogramCache != image && !samePixels(previous, e.spectrogramCache), "Global custom colour changes must recolour spectral history");
        image = e.spectrogramCache; previous = image.createCopy();
        e.processor.themeMode.store(2); e.customizationChanged();
        require(e.spectrogramCache != image && !samePixels(previous, e.spectrogramCache), "Theme changes must update the spectral finishing map");
        image = e.spectrogramCache; previous = image.createCopy();
        e.frame.sampleRate = 96000; e.rebuildSpectrogram();
        require(e.spectrogramCache != image && !samePixels(previous, e.spectrogramCache), "Sample-rate changes must reproject retained FFT bins");

        spectralBase(e); e.lastHistoryTime = e.meterTime; e.meterTime += .0501;
        AnalysisFrame incoming; incoming.sampleRate = e.frame.sampleRate;
        incoming.spectrum = e.history.back(); for (auto& db : incoming.spectrum) db -= 6;
        image = e.spectrogramCache; e.updateVisualHistory(incoming);
        require(e.spectrogramCache == image, "Appending spectral history must update columns in the existing cache");
        const auto incremental = e.spectrogramCache.createCopy();
        e.rebuildSpectrogram();
        require(e.spectrogramCache == image && samePixels(incremental, e.spectrogramCache), "Appearance caching must retain newly appended spectral columns");
        e.spectrogramAppearanceValid = false; e.rebuildSpectrogram();
        require(samePixels(incremental, e.spectrogramCache), "Incremental spectral append must match a full rebuild exactly");
    }
};

int main() {
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    auto* report = new juce::DynamicObject;
    juce::var ownedReport(report);
    int exit = 0;
    try {
        AuroraAudioProcessor processor;
        AuroraAudioProcessorEditor editor(processor);
        EditorPerformanceProbe::stop(editor);
        require(!editor.isShowing(), "CPU benchmark must never open a native editor window");
        EditorPerformanceProbe::unavailableGraphicsTests();
        EditorPerformanceProbe::expandedSceneTests(editor);
        EditorPerformanceProbe::stereoTests(editor);
        EditorPerformanceProbe::spectrogramTests(editor);
        report->setProperty("passed", true);
    } catch (const std::exception& error) {
        report->setProperty("passed", false); report->setProperty("error", error.what());
        std::cerr << "Editor performance regression failed: " << error.what() << '\n'; exit = 1;
    }
    report->setProperty("checks", checks); report->setProperty("benchmarks", juce::var(results));
    report->setProperty("operatingSystem", juce::SystemStats::getOperatingSystemName());
    report->setProperty("nativeWindowOpened", false); report->setProperty("audioDeviceOpened", false);
    report->setProperty("measurementScope", "Bounded CPU-only production editor cache/raster tests; timings are local observations, not a real-time audio or end-to-end frame-rate guarantee.");
    const auto directory = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("aurora-editor-performance");
    if (!directory.createDirectory().wasOk() || !directory.getChildFile("report.json").replaceWithText(juce::JSON::toString(ownedReport, true))) exit = 1;
    std::cout << "Editor performance regression: " << checks << " checks, exit " << exit << '\n';
    return exit;
}
