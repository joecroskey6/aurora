#include "FactoryPresets.h"
#include "PluginProcessor.h"
#include "WaveformMusic.h"
#include <array>
#include <functional>
#include <iostream>
#include <set>
#include <vector>

// Native production-editor QA. Synthetic PCM enters the analyzer directly;
// this executable never opens an audio device or writes the user's presets.
namespace {
using namespace juce;
const File outputDirectory("/tmp/aurora-factory-presets-qa");

class FactoryPresetTestApp final : public JUCEApplication, private Timer {
public:
    const String getApplicationName() override { return "Aurora Factory Preset Tests"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const String& arguments) override {
        captureGl = arguments.contains("--capture-gl") || arguments.contains("--gl-probe");
        glProbe = arguments.contains("--gl-probe");
        if (!outputDirectory.createDirectory().wasOk()) { setApplicationReturnValue(1); quit(); return; }
        processor = std::make_unique<AuroraAudioProcessor>();
        for (int i = 0; i < factoryPresetCount(); ++i) {
            AuroraAudioProcessor temporary;
            check("Factory XML " + String(i) + " loads", temporary.applyAppearancePreset(factoryPresetXml(i)));
            canonical.push_back(temporary.createAppearancePreset());
        }
        // A full PCM history plus twelve seconds sampled by the real editor
        // fills the scrolling waveform and spectrogram before any captures.
        while (sourceSample < 32 * 48000) feedBlock();
        processor->isReceiving.store(true);
        window = std::make_unique<Window>();
        editor = processor->createEditor(); window->setContentOwned(editor, true);
        window->setContentComponentSize(1280, 820); window->centreWithSize(window->getWidth(), window->getHeight());
        window->setVisible(true); window->toFront(true);
        check("Production preset selector exists", selector() != nullptr);
        if (selector()) check("Production selector includes every factory look", selector()->getNumItems() == factoryPresetCount());
        addSteps();
        if (glProbe) steps.resize(1);
        started = Time::getMillisecondCounterHiRes() * .001;
        nextAudioAt = started; warmUntil = started + 12.1; nextAt = warmUntil + .2;
        startTimerHz(60);
    }
    void shutdown() override { stopTimer(); window.reset(); editor = nullptr; processor.reset(); }
    void systemRequestedQuit() override { finish("Window closed before native preset QA completed"); }
private:
    struct Window final : DocumentWindow {
        Window() : DocumentWindow("Aurora Factory Presets QA", Colour(0xff111c1b), allButtons) { setUsingNativeTitleBar(true); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    struct Step {
        String name;
        std::function<void()> configure;
        std::function<void()> verify;
        bool captureEditor = false, captureGallery = false;
    };
    std::unique_ptr<AuroraAudioProcessor> processor;
    std::unique_ptr<Window> window;
    AudioProcessorEditor* editor = nullptr;
    std::vector<String> canonical;
    std::vector<Step> steps;
    Array<var> checks, captures, gpuCaptures;
    int failures = 0, stepIndex = 0;
    bool waitingForPaint = false, finished = false, warmed = false, captureGl = false, glProbe = false;
    std::uint64_t sourceSample = 0, frozenSamples = 0, frozenSequence = 0;
    double started = 0, nextAt = 0, nextAudioAt = 0, warmUntil = 0;
    Image previousEditorPixels;
    int desktopWindowsBeforeCustomise = 0;
    void* sceneContextBeforeCustomise = nullptr;

    void check(const String& label, bool passed) {
        auto* record = new DynamicObject; record->setProperty("name", label); record->setProperty("passed", passed);
        checks.add(var(record)); if (!passed) ++failures;
        std::cout << (passed ? "PASS " : "FAIL ") << label << std::endl;
    }
    void feedBlock() {
        std::array<float, 800> left {}, right {};
        for (std::size_t i = 0; i < left.size(); ++i) {
            const auto sample = waveform_test_music::sample(sourceSample + i, 48000);
            left[i] = sample.left; right[i] = sample.right;
        }
        processor->analyzer.process(left.data(), right.data(), int(left.size()), 48000, 2);
        sourceSample += left.size();
    }
    static Component* find(Component& root, const String& id) {
        if (root.getComponentID() == id) return &root;
        for (auto* child : root.getChildren()) if (auto* result = find(*child, id)) return result;
        return nullptr;
    }
    static Component* findTitle(Component& root, const String& title) {
        if (root.getTitle() == title) return &root;
        for (auto* child : root.getChildren()) if (auto* result = findTitle(*child, title)) return result;
        return nullptr;
    }
    static Component* findDesktop(const String& id) {
        auto& desktop = Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (auto* root = desktop.getComponent(i)) if (auto* result = find(*root, id)) return result;
        return nullptr;
    }
    ComboBox* selector() const { return dynamic_cast<ComboBox*>(find(*editor, "factoryPresetChoice")); }
    static OpenGLContext* sceneContext(Component& root) {
        if (auto* context = OpenGLContext::getContextAttachedTo(root)) return context;
        for (auto* child : root.getChildren()) if (auto* context = sceneContext(*child)) return context;
        return nullptr;
    }
    void checkSceneContext(const String& action) {
        auto* context = sceneContext(*editor);
        check(action + " preserves the existing OpenGL context", sceneContextBeforeCustomise != nullptr && context
            && context->getRawContext() == sceneContextBeforeCustomise);
    }
    void click(Component* component, const String& label) {
        auto* button = dynamic_cast<Button*>(component);
        check(label + " button exists", button != nullptr);
        if (button) button->triggerClick();
    }
    void selectPreset(int index) {
        if (auto* choice = selector()) choice->setSelectedId(index + 1, sendNotificationSync);
    }
    static int desktopWindowCount() {
        int count = 0; auto& desktop = Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (dynamic_cast<DocumentWindow*>(desktop.getComponent(i))) ++count;
        return count;
    }
    void verifyPreset(int index) {
        check("Selected look " + String(factoryPresetName(index)) + " exactly applied",
              processor->createAppearancePreset() == canonical[std::size_t(index)]);
        check("Preset header follows " + String(factoryPresetName(index)),
              selector() && selector()->getSelectedId() == index + 1);
        check("Selecting " + String(factoryPresetName(index)) + " preserves freeze and measured PCM",
              processor->frozen.load() && processor->analyzer.waveformSnapshot().totalSamples == frozenSamples
              && processor->analyzer.snapshot().sequence == frozenSequence);
    }
    static String slug(const String& name) {
        return name.toLowerCase().replaceCharacter(' ', '-').retainCharacters("abcdefghijklmnopqrstuvwxyz0123456789-");
    }
    void addSteps() {
        for (int i = 0; i < factoryPresetCount(); ++i) {
            steps.push_back({String(i).paddedLeft('0', 2) + "-" + slug(factoryPresetName(i)),
                [this, i] { selectPreset(i); }, [this, i] { verifyPreset(i); }, true, false});
        }
        steps.push_back({"next-wraps-to-first", [this] { click(find(*editor, "factoryPresetNext"), "Next look"); },
            [this] { verifyPreset(0); }, false, false});
        steps.push_back({"previous-wraps-to-last", [this] { click(find(*editor, "factoryPresetPrevious"), "Previous look"); },
            [this] { verifyPreset(factoryPresetCount() - 1); }, false, false});
        steps.push_back({"modified-look", [this] {
            processor->custom.set("waveGain", processor->custom.get("waveGain") < 3 ? 3.5f : .5f);
        }, [this] {
            check("Modifying a look clears its exact-match selection", selector() && selector()->getSelectedId() == 0);
        }, true, false});
        steps.push_back({"restore-modified-look", [this] { selectPreset(factoryPresetCount() - 1); },
            [this] { verifyPreset(factoryPresetCount() - 1); }, false, false});
        std::set<int> layouts;
        for (int i = 0; i < factoryPresetCount(); ++i) if (layouts.insert(factoryPresetLayout(i)).second) {
            steps.push_back({"compact-" + slug(factoryPresetName(i)), [this, i] {
                selectPreset(i); editor->setSize(1100, factoryPresetLayout(i) == 1 ? 390 : factoryPresetLayout(i) == 2 ? 570 : 620);
                editor->resized();
            }, [this, i] { verifyPreset(i); }, true, false});
        }
        steps.push_back({"open-customise", [this] {
            desktopWindowsBeforeCustomise = desktopWindowCount();
            if (auto* context = sceneContext(*editor)) sceneContextBeforeCustomise = context->getRawContext();
            check("Scene context exists before opening Customise", sceneContextBeforeCustomise != nullptr);
            click(findTitle(*editor, "Customise Aurora"), "Customise");
        }, [this] {
            auto* panel = find(*editor, "customizationPanel");
            auto* dropdown = dynamic_cast<Viewport*>(find(*editor, "customizationDropdown"));
            check("Real editor opens its embedded customization panel", panel && panel->isShowing() && dropdown && dropdown->isShowing());
            check("Factory collection stays in the plugin's native peer", panel && panel->getPeer() == editor->getPeer());
            check("Opening Collection creates no additional native window", desktopWindowCount() == desktopWindowsBeforeCustomise);
            checkSceneContext("Opening Customise");
        }, true, false});
        steps.push_back({"factory-gallery-top", [this] { click(findDesktop("navPresets"), "Presets navigation"); }, [this] {
            auto* browser = findDesktop("factoryPresetBrowser");
            check("Presets opens the bundled factory gallery", browser && browser->isShowing());
            for (int i = 0; i < factoryPresetCount(); ++i) {
                auto* card = findDesktop("factoryPreset_" + String(i));
                check("Gallery contains " + String(factoryPresetName(i)), card && !card->getBounds().isEmpty());
                if (card && card->getParentComponent()) check("Factory card " + String(i) + " fits its scroll content",
                    card->getParentComponent()->getLocalBounds().contains(card->getBounds()));
            }
        }, false, true});
        for (int i : {0, factoryPresetCount() / 2, factoryPresetCount() - 1}) {
            steps.push_back({"gallery-select-" + slug(factoryPresetName(i)), [this, i] {
                auto* card = findDesktop("factoryPreset_" + String(i));
                if (card) for (auto* parent = card->getParentComponent(); parent; parent = parent->getParentComponent())
                    if (auto* viewport = dynamic_cast<Viewport*>(parent)) {
                        const auto area = viewport->getViewedComponent()->getLocalArea(card, card->getLocalBounds());
                        viewport->setViewPosition(0, std::max(0, area.getY() - 10)); break;
                    }
                click(card, "Factory card " + String(i));
            }, [this, i] {
                verifyPreset(i);
                for (int cardIndex = 0; cardIndex < factoryPresetCount(); ++cardIndex) {
                    auto* card = dynamic_cast<Button*>(findDesktop("factoryPreset_" + String(cardIndex)));
                    check("Gallery selected state " + String(cardIndex) + " follows look " + String(i),
                          card && card->getToggleState() == (cardIndex == i));
                }
            }, true, true});
        }
        steps.push_back({"saved-presets-tab", [this] { click(findDesktop("savedPresetTab"), "Saved tab"); }, [this] {
            auto* library = findDesktop("presetLibrary"); auto* browser = findDesktop("factoryPresetBrowser");
            check("Saved preset library remains accessible separately", library && library->isShowing() && browser && !browser->isShowing());
        }, false, true});
        steps.push_back({"factory-gallery-return", [this] { click(findDesktop("factoryPresetTab"), "Factory tab"); }, [this] {
            auto* browser = findDesktop("factoryPresetBrowser");
            check("Returning to Factory preserves the selected look", browser && browser->isShowing()
                  && processor->createAppearancePreset() == canonical.back());
        }, false, true});
        steps.push_back({"factory-gallery-compact", [this] {
            // Host-driven layout changes keep the open collection in place.
            processor->layoutMode.store(1); editor->setSize(1100, 390); editor->resized();
        }, [this] {
            auto* browser = findDesktop("factoryPresetBrowser");
            auto* first = findDesktop("factoryPreset_0"); auto* second = findDesktop("factoryPreset_1");
            check("Compact gallery remains visible", browser && browser->isShowing());
            check("Embedded gallery keeps readable cards across the plugin width", first && second
                  && first->getWidth() >= 220 && second->getWidth() >= 220 && first->getBounds() != second->getBounds());
            auto* dropdown = dynamic_cast<Viewport*>(find(*editor, "customizationDropdown"));
            check("Compact collection dropdown remains inside the plugin", dropdown && editor->getLocalBounds().contains(dropdown->getBounds()));
            check("Compact collection has scroll access to its full content", dropdown && dropdown->getViewedComponent()
                  && dropdown->getViewedComponent()->getHeight() >= 520 && dropdown->getViewedComponent()->getHeight() > dropdown->getViewHeight());
            if (auto* viewport = dynamic_cast<Viewport*>(findDesktop("factoryPresetViewport"))) viewport->setViewPosition(0, 0);
        }, true, true});
        steps.push_back({"factory-gallery-close", [this] {
            click(findTitle(*editor, "Customise Aurora"), "Customise close");
        }, [this] {
            auto* dropdown = find(*editor, "customizationDropdown");
            check("Customise closes in place after browsing", dropdown && !dropdown->isVisible());
            checkSceneContext("Closing Customise after browsing");
        }, true, false});
        steps.push_back({"factory-gallery-reopen", [this] {
            click(findTitle(*editor, "Customise Aurora"), "Customise reopen");
        }, [this] {
            auto* dropdown = find(*editor, "customizationDropdown");
            check("Customise reopens the same embedded collection", dropdown && dropdown->isShowing());
            checkSceneContext("Reopening Customise");
        }, false, false});
        steps.push_back({"factory-gallery-close-again", [this] {
            click(findTitle(*editor, "Customise Aurora"), "Customise close again");
        }, [this] { checkSceneContext("Repeated Customise toggles"); }, false, false});
    }
    void captureGpuSurfaces(Component& root, Component& node, Image& destination, const String& name) {
        using namespace juce::gl;
        if (!node.isShowing()) return;
        if (auto* context = OpenGLContext::getContextAttachedTo(node)) {
            // All component geometry is read on the message thread. The GL
            // callback only reads framebuffer pixels and restores GL state.
            const auto fullArea = root.getLocalArea(&node, node.getLocalBounds());
            auto clippedArea = fullArea.getIntersection(root.getLocalBounds());
            for (auto* parent = node.getParentComponent(); parent && parent != &root; parent = parent->getParentComponent())
                clippedArea = clippedArea.getIntersection(root.getLocalArea(parent, parent->getLocalBounds()));
            if (clippedArea.isEmpty()) return;
            int width = 0, height = 0;
            GLenum beforeError = GL_NO_ERROR, readError = GL_NO_ERROR;
            std::vector<GLubyte> rgba;
            context->executeOnGLThread([&](OpenGLContext& active) {
                beforeError = glGetError();
                GLint viewport[4] {}, previousFramebuffer = 0, previousPackBuffer = 0;
                GLint packAlignment = 0, packRowLength = 0, packSkipRows = 0, packSkipPixels = 0;
                glGetIntegerv(GL_VIEWPORT, viewport);
                width = viewport[2]; height = viewport[3];
                if (width < 1 || height < 1 || width > 8192 || height > 8192) return;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFramebuffer);
                glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer);
                glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment);
                glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
                glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows);
                glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
                glBindFramebuffer(GL_READ_FRAMEBUFFER, active.getFrameBufferID());
                GLint defaultReadBuffer = 0;
                glGetIntegerv(GL_READ_BUFFER, &defaultReadBuffer);
                // macOS uses framebuffer zero with a double-buffered native
                // NSOpenGLView. FRONT is the last completed, presented image;
                // reading BACK here would observe an undefined swap buffer.
                glReadBuffer(active.getFrameBufferID() == 0 ? GL_FRONT : GL_COLOR_ATTACHMENT0);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
                glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
                glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
                rgba.resize(std::size_t(width) * std::size_t(height) * 4);
                glFinish();
                glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
                readError = glGetError();
                glPixelStorei(GL_PACK_ALIGNMENT, packAlignment); glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
                glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows); glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
                glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(previousPackBuffer));
                glReadBuffer(GLenum(defaultReadBuffer));
                glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousFramebuffer));
            }, true);
            check(name + " GL readback completed without errors", beforeError == GL_NO_ERROR && readError == GL_NO_ERROR
                  && width > 0 && height > 0 && rgba.size() == std::size_t(width) * std::size_t(height) * 4);
            if (beforeError != GL_NO_ERROR || readError != GL_NO_ERROR || rgba.empty()) return;
            Image scene(Image::RGB, width, height, false);
            std::set<std::uint32_t> sampledColours;
            {
                Image::BitmapData bitmap(scene, Image::BitmapData::writeOnly);
                for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                    const auto offset = (std::size_t(height - 1 - y) * std::size_t(width) + std::size_t(x)) * 4;
                    const auto colour = Colour(rgba[offset], rgba[offset + 1], rgba[offset + 2]);
                    bitmap.setPixelColour(x, y, colour);
                    if (x % 4 == 0 && y % 4 == 0) sampledColours.insert(colour.getARGB());
                }
            }
            check(name + " GL front buffer contains a detailed rendered scene", sampledColours.size() >= 32);
            if (sampledColours.size() < 32) return;
            {
                Graphics composite(destination);
                composite.reduceClipRegion(clippedArea);
                composite.drawImage(scene, fullArea.toFloat());
            }
            const auto file = outputDirectory.getChildFile(name + "-gpu.png"); file.deleteFile();
            bool saved = false;
            if (auto stream = file.createOutputStream()) { saved = PNGImageFormat().writeImageToStream(scene, *stream); stream->flush(); }
            check(name + " native-resolution GPU PNG saved", saved);
            auto* record = new DynamicObject; record->setProperty("name", name); record->setProperty("file", file.getFullPathName());
            record->setProperty("width", width); record->setProperty("height", height); record->setProperty("sampledColours", int(sampledColours.size()));
            record->setProperty("editorBounds", fullArea.toString()); record->setProperty("clipBounds", clippedArea.toString());
            record->setProperty("beforeGlError", int(beforeError)); record->setProperty("readGlError", int(readError));
            gpuCaptures.add(var(record));
        }
        for (auto* child : node.getChildren()) captureGpuSurfaces(root, *child, destination, name);
    }
    void capture(Component& component, const String& name, bool gallery) {
        auto pixels = component.createComponentSnapshot(component.getLocalBounds(), true, 1.f);
        if (captureGl && !gallery) captureGpuSurfaces(component, component, pixels, name);
        const auto file = outputDirectory.getChildFile(name + ".png"); file.deleteFile();
        bool saved = false;
        if (auto stream = file.createOutputStream()) { saved = PNGImageFormat().writeImageToStream(pixels, *stream); stream->flush(); }
        check(name + " native PNG saved", pixels.isValid() && saved);
        if (!gallery) {
            for (auto* child : component.getChildren()) {
                if (!child->isVisible()) continue;
                check(name + " control fits: " + (child->getComponentID().isNotEmpty() ? child->getComponentID() : child->getTitle()),
                      !child->getBounds().isEmpty() && component.getLocalBounds().contains(child->getBounds()));
            }
            const bool factoryLook = name.length() > 2 && CharacterFunctions::isDigit(name[0])
                                     && CharacterFunctions::isDigit(name[1]) && name[2] == '-';
            if (factoryLook && previousEditorPixels.isValid() && previousEditorPixels.getBounds() == pixels.getBounds()) {
                int changed = 0;
                // Skip the header: title changes alone cannot satisfy this.
                for (int y = 120; y < pixels.getHeight(); y += 3)
                    for (int x = 0; x < pixels.getWidth(); x += 3)
                        if (pixels.getPixelAt(x, y) != previousEditorPixels.getPixelAt(x, y)) ++changed;
                check(name + " visibly changes meter or layout pixels", changed > 100);
            }
            if (factoryLook) previousEditorPixels = pixels;
        }
        auto* record = new DynamicObject; record->setProperty("name", name); record->setProperty("file", file.getFullPathName());
        record->setProperty("width", pixels.getWidth()); record->setProperty("height", pixels.getHeight());
        record->setProperty("gallery", gallery); record->setProperty("layout", processor->layoutMode.load());
        record->setProperty("factoryPresetId", selector() ? selector()->getSelectedId() - 1 : -1);
        record->setProperty("elapsedSeconds", Time::getMillisecondCounterHiRes() * .001 - started);
        captures.add(var(record));
    }
    void timerCallback() override {
        const double now = Time::getMillisecondCounterHiRes() * .001;
        if (!warmed) {
            while (nextAudioAt <= now && nextAudioAt < warmUntil) { feedBlock(); nextAudioAt += 1.0 / 60; }
            if (now < warmUntil) return;
            processor->frozen.store(true); warmed = true;
            frozenSamples = processor->analyzer.waveformSnapshot().totalSamples;
            frozenSequence = processor->analyzer.snapshot().sequence;
            check("Measured music and spectrogram warmed before preset browsing", frozenSamples >= 44 * 48000);
        }
        if (now < nextAt) return;
        if (stepIndex >= int(steps.size())) { finish(); return; }
        auto& step = steps[std::size_t(stepIndex)];
        if (!waitingForPaint) { step.configure(); waitingForPaint = true; nextAt = now + .55; return; }
        if (step.verify) step.verify();
        if (step.captureEditor) capture(*editor, step.name + "-editor", false);
        if (step.captureGallery) {
            auto* panel = findDesktop("customizationPanel");
            check(step.name + " settings panel available for capture", panel != nullptr);
            if (panel) capture(*panel, step.name + "-gallery", true);
        }
        waitingForPaint = false; ++stepIndex; nextAt = Time::getMillisecondCounterHiRes() * .001 + .06;
    }
    void finish(const String& error = {}) {
        if (finished) return; finished = true; stopTimer();
        if (error.isNotEmpty()) check(error, false);
        check("Browsing finishes without modifying measured audio", warmed
              && processor->analyzer.waveformSnapshot().totalSamples == frozenSamples
              && processor->analyzer.snapshot().sequence == frozenSequence);
        if (captureGl) check("At least one production OpenGL surface was captured", !gpuCaptures.isEmpty());
        auto* report = new DynamicObject; report->setProperty("passed", failures == 0);
        report->setProperty("failures", failures); report->setProperty("checks", var(checks)); report->setProperty("captures", var(captures));
        report->setProperty("gpuCaptures", var(gpuCaptures));
        report->setProperty("factoryPresetCount", factoryPresetCount()); report->setProperty("audioDeviceOpened", false);
        report->setProperty("userPresetLibraryModified", false); report->setProperty("syntheticPcmSamples", var(int64(frozenSamples)));
        report->setProperty("captureMethod", captureGl
            ? "Actual production JUCE editor snapshots with direct readback of each visible OpenGL surface's presented front buffer on its GL thread, vertically oriented and composited at its real clipped editor bounds. Separate GPU PNG files retain native physical resolution. Gallery cards are illustrative previews."
            : "Actual production JUCE editor and customization gallery snapshots. OpenGL scene pixels are excluded by JUCE component snapshots; GPU scene rendering is validated separately.");
        report->setProperty("sourceFixture", "Original procedural stereo music, twelve seconds of live editor warmup, then frozen measured PCM and meter history for all look comparisons.");
        if (!outputDirectory.getChildFile("report.json").replaceWithText(JSON::toString(var(report), true))) ++failures;
        std::cout << "Factory preset native QA: " << failures << " failures, " << captures.size() << " captures\n";
        setApplicationReturnValue(failures ? 1 : 0); quit();
    }
};
} // namespace
START_JUCE_APPLICATION(FactoryPresetTestApp)
