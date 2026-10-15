#include "VisualSurface.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <algorithm>
#include <atomic>
#include <iostream>
#include <numeric>

// Native, synthetic-input GPU benchmark. Explicit synchronization belongs only
// in this test: the plug-in never waits for GPU completion on the message thread.
class VisualSurfacePerformanceProbe {
public:
    static juce::var run(VisualSurface& surface, bool baseline) {
        using namespace juce;
        using namespace juce::gl;
        auto* report = new DynamicObject;
        report->setProperty("method", "Production VisualSurface; 720x450 logical pixels; 30 frames per scene; glFinish wall time; synthetic input; no audio device");
        report->setProperty("baseline", baseline);
        report->setProperty("baselineMethod", "When --baseline is set, reproduce the previous unused PearlFlow updates before each non-Pearl scene. All other rendering uses the same current production path.");
        report->setProperty("renderingScale", surface.context.getRenderingScale());
        report->setProperty("renderer", String::fromUTF8(reinterpret_cast<const char*>(glGetString(GL_RENDERER))));
        int failures = surface.ready.load() ? 0 : 1;
        Array<var> scenes;
        for (int mode = 0; mode < 6; ++mode) {
            {
                std::lock_guard<std::mutex> lock(surface.mutex);
                auto& s = surface.scene;
                s.visual = mode; s.theme = 1; s.low = .55f; s.mid = .45f; s.high = .25f;
                s.activity = .8f; s.onset = .1f; s.frozen = false;
                s.colours = {0xb0acff, 0x82dfce, 0xffead1};
                for (int i = 0; i < 128; ++i) {
                    s.bands[size_t(i)] = .2f + .2f * std::sin(float(i) * .1f);
                    s.wave[size_t(i)] = .5f * std::sin(float(i) * .2f);
                    s.right[size_t(i)] = .5f * std::sin(float(i) * .2f + .4f);
                }
            }
            auto draw = [&] {
                surface.scene.phase += 1.f / 30.f;
                // Use a fixed integration interval for an apples-to-apples
                // workload; scheduling jitter must not change solver substeps.
                surface.lastRenderTime = Time::getMillisecondCounterHiRes() * .001 - 1.0 / 30.0;
                if (baseline && mode != 2) {
                    if (!surface.flowInitialisationAttempted) {
                        surface.flowInitialisationAttempted = true;
                        surface.pearlFlow.initialise(surface.context);
                    }
                    const auto& s = surface.scene;
                    PearlFlow::Input input;
                    input.dt = 1.f / 30.f; input.phase = s.phase;
                    input.low = s.low; input.mid = s.mid; input.high = s.high;
                    input.gain = s.gain; input.correlation = s.correlation; input.onset = s.onset;
                    input.width = 848; input.height = 512; input.resetGeneration = s.generation;
                    surface.pearlFlow.update(input);
                }
                surface.renderOpenGL();
            };
            for (int i = 0; i < 4; ++i) draw();
            glFinish();
            const auto readDye = [&] {
                std::vector<float> pixels;
                if (surface.pearlFlow.isReady()) {
                    pixels.resize(size_t(surface.pearlFlow.getDyeWidth()) * size_t(surface.pearlFlow.getDyeHeight()) * 4);
                    GLint previous = 0;
                    glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
                    glBindTexture(GL_TEXTURE_2D, surface.pearlFlow.getDyeTexture());
                    glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
                    glBindTexture(GL_TEXTURE_2D, GLuint(previous));
                }
                return pixels;
            };
            const auto dyeBefore = readDye();
            std::vector<double> samples;
            for (int i = 0; i < 30; ++i) {
                const auto start = Time::getMillisecondCounterHiRes();
                draw(); glFinish();
                samples.push_back(Time::getMillisecondCounterHiRes() - start);
            }
            std::sort(samples.begin(), samples.end());
            auto* scene = new DynamicObject;
            scene->setProperty("mode", mode);
            scene->setProperty("meanMs", std::accumulate(samples.begin(), samples.end(), 0.0) / samples.size());
            scene->setProperty("medianMs", (samples[14] + samples[15]) * .5);
            scene->setProperty("p95Ms", samples[28]);
            const bool fluidUnchanged = dyeBefore == readDye();
            scene->setProperty("fluidUnchanged", fluidUnchanged);
            if (!baseline && mode != 2 && !fluidUnchanged) ++failures;
            if (mode == 2 && fluidUnchanged) ++failures;
            scene->setProperty("glError", int(glGetError()));
            if (int(scene->getProperty("glError")) != 0) ++failures;
            scenes.add(var(scene));
            std::cout << JSON::toString(var(scene), true).toStdString() << std::endl;
        }
        report->setProperty("scenes", var(scenes));
        report->setProperty("failures", failures);
        return var(report);
    }
};

class VisualSurfacePerformanceApp final : public juce::JUCEApplication, private juce::Timer {
public:
    const juce::String getApplicationName() override { return "Aurora Surface Performance"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    void initialise(const juce::String& args) override {
        baseline = args.contains("--baseline");
        window = std::make_unique<juce::DocumentWindow>("Aurora render benchmark", juce::Colours::black, 0);
        surface = new VisualSurface;
        surface->setSize(720, 450);
        window->setContentOwned(surface, true);
        window->centreWithSize(720, 450);
        window->setVisible(true);
        started = juce::Time::getMillisecondCounterHiRes();
        startTimer(100);
    }
    void shutdown() override { stopTimer(); window.reset(); }
private:
    void timerCallback() override {
        if (done.load()) {
            setApplicationReturnValue(failures); stopTimer(); quit(); return;
        }
        if (juce::Time::getMillisecondCounterHiRes() - started > 60000) {
            setApplicationReturnValue(1); stopTimer(); quit(); return;
        }
        if (submitted) return;
        if (auto* context = juce::OpenGLContext::getContextAttachedTo(*surface)) {
            submitted = true;
            context->executeOnGLThread([this](juce::OpenGLContext&) {
                auto result = VisualSurfacePerformanceProbe::run(*surface, baseline);
                const auto output = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(
                    baseline ? "aurora-surface-baseline.json" : "aurora-surface-performance.json");
                if (!output.replaceWithText(juce::JSON::toString(result))) ++failures;
                failures += int(result.getProperty("failures", 1));
                done.store(true);
            }, false);
        }
    }
    std::unique_ptr<juce::DocumentWindow> window;
    VisualSurface* surface = nullptr;
    std::atomic<bool> done{false};
    double started = 0;
    int failures = 0;
    bool baseline = false, submitted = false;
};
START_JUCE_APPLICATION(VisualSurfacePerformanceApp)
