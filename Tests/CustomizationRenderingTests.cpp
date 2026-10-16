#include "CustomizationPanel.h"
#include "PluginProcessor.h"
#include "WaveformMusic.h"
#include <array>
#include <functional>
#include <iostream>
#include <set>
#include <utility>
#include <vector>

// Developer-only native settings/editor QA. No audio device, host, preset
// library writes, import dialogs or export dialogs are used by this runner.
namespace {
using namespace juce;
const auto outputDirectory = File::getSpecialLocation(File::tempDirectory).getChildFile("aurora-customization-qa");
constexpr std::array<const char*, 9> sections {
    "Colours", "Waveform", "Spectrum", "Spectrogram", "Stereo", "Levels", "Scene", "Layout", "Presets"
};

class CustomizationTestApp final : public JUCEApplication, private Timer {
public:
    const String getApplicationName() override { return "Aurora Customization Tests"; }
    const String getApplicationVersion() override { return "0.1.0"; }
    void initialise(const String&) override {
        if (!outputDirectory.createDirectory().wasOk()) { setApplicationReturnValue(1); quit(); return; }
        processor = std::make_unique<AuroraAudioProcessor>();
        processor->layoutMode.store(2); processor->isReceiving.store(false);
        std::array<float, 800> left {}, right {};
        for (std::uint64_t first = 0; first < 32 * 48000; first += left.size()) {
            for (std::size_t i = 0; i < left.size(); ++i) {
                const auto s = waveform_test_music::sample(first + i, 48000);
                left[i] = s.left; right[i] = s.right;
            }
            processor->analyzer.process(left.data(), right.data(), int(left.size()), 48000, 2);
        }
        check("32 seconds of original procedural PCM analyzed", processor->analyzer.waveformSnapshot().totalSamples == 32 * 48000);
        editorWindow = std::make_unique<Window>("Aurora Customization Editor QA");
        editor = processor->createEditor(); editorWindow->setContentOwned(editor, true);
        editorWindow->setContentComponentSize(1280, 740); editorWindow->centreWithSize(editorWindow->getWidth(), editorWindow->getHeight());
        editorWindow->setVisible(true);
        settingsWindow = std::make_unique<Window>("Aurora Customization Panel QA");
        auto settings = createCustomizationPanel(*processor, [this] { ++changeNotifications; editor->resized(); editor->repaint(); });
        panel = settings.get(); settingsWindow->setContentOwned(settings.release(), true);
        settingsWindow->setContentComponentSize(560, 680); settingsWindow->centreWithSize(settingsWindow->getWidth(), settingsWindow->getHeight());
        settingsWindow->setVisible(true); settingsWindow->toFront(true);
        addSteps();
        started = Time::getMillisecondCounterHiRes() * .001; nextAt = started + .45;
        startTimerHz(30);
    }
    void shutdown() override { stopTimer(); settingsWindow.reset(); panel = nullptr; editorWindow.reset(); editor = nullptr; processor.reset(); }
    void systemRequestedQuit() override { finish("Window closed before native QA completed"); }
private:
    struct Window final : DocumentWindow {
        explicit Window(const String& title) : DocumentWindow(title, Colour(0xff111c1b), allButtons) { setUsingNativeTitleBar(true); }
        void closeButtonPressed() override { JUCEApplication::getInstance()->systemRequestedQuit(); }
    };
    struct Step { String name; std::function<void()> configure; bool capturePanel = false, captureEditor = false; std::function<void()> verify; };
    std::unique_ptr<AuroraAudioProcessor> processor;
    std::unique_ptr<Window> editorWindow, settingsWindow;
    AudioProcessorEditor* editor = nullptr;
    Component* panel = nullptr;
    Component::SafePointer<Viewport> editorSettings;
    int desktopWindowsBeforeCustomise = 0;
    struct SceneExpansionBaseline {
        String appearance;
        Rectangle<int> editorBounds, windowBounds;
        Component::SafePointer<Component> clip, surface, parent;
        ComponentPeer* peer = nullptr;
        OpenGLContext* context = nullptr;
        void* nativeContext = nullptr;
        std::vector<std::pair<Component::SafePointer<Component>, bool>> controls;
        int layout = 0, desktopWindows = 0;
        double scrollOffset = 0;
        bool frozen = false, settingsOpen = false;
        std::uint64_t meterGeneration = 0;
    } sceneBaseline;
    int nativeContextComparisons = 0;
    std::vector<Step> steps;
    Array<var> checks, captures, gpuCaptures;
    int failures = 0, changeNotifications = 0, index = 0;
    bool waitingForPaint = false, finished = false;
    double started = 0, nextAt = 0;

    void check(const String& label, bool passed) {
        auto* record = new DynamicObject; record->setProperty("name", label); record->setProperty("passed", passed);
        checks.add(var(record)); if (!passed) ++failures;
        std::cout << (passed ? "PASS " : "FAIL ") << label << std::endl;
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
            if (auto* component = desktop.getComponent(i))
                if (auto* result = find(*component, id)) return result;
        return nullptr;
    }
    void setToggle(const String& id, bool enabled) {
        auto* toggle = dynamic_cast<ToggleButton*>(find(*panel, id + "Value"));
        check(id + " toggle exists", toggle != nullptr);
        if (toggle) toggle->setToggleState(enabled, sendNotificationSync);
    }
    Image waveformPixels() {
        const float areaWidth = float(editor->getWidth() - 32);
        const int width = int(areaWidth * (1.f - processor->custom.get("layoutLevelWidth")) - processor->custom.get("layoutGap"));
        // Exclude the title and child selectors: changes must come from the
        // measured plot/labels, not a control's displayed value or focus ring.
        return editor->createComponentSnapshot({16, 152, width, editor->getHeight() - 168}, true, 1.f);
    }
    static int changedPixels(const Image& a, const Image& b) {
        if (a.getBounds() != b.getBounds()) return -1;
        int count = 0;
        for (int y = 0; y < a.getHeight(); ++y)
            for (int x = 0; x < a.getWidth(); ++x)
                if (a.getPixelAt(x, y) != b.getPixelAt(x, y)) ++count;
        return count;
    }
    void verifyScopePixels() {
        processor->custom.resetSection(Customization::Section::Waveform);
        processor->waveformWindow.store(3);
        const auto baseline = waveformPixels();
        check("Frozen Scope redraw is identical", changedPixels(baseline, waveformPixels()) == 0);
        for (const auto* key : {"waveGrid", "waveLabels", "waveCrest"}) {
            processor->custom.set(key, 0);
            check(String("Scope renders ") + key + " changes", changedPixels(baseline, waveformPixels()) > 20);
            processor->custom.set(key, 1);
        }
        // A wide Scope can show one sample per pixel, where the signed
        // min/max envelope correctly has no interior. Studio's narrower panel
        // exercises the fill against real multi-sample columns instead.
        processor->layoutMode.store(0); editor->resized();
        processor->custom.set("waveFill", 0);
        const auto withoutFill = editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.f);
        processor->custom.set("waveFill", 1);
        check("Scope fill changes a downsampled min/max envelope", changedPixels(withoutFill,
            editor->createComponentSnapshot(editor->getLocalBounds(), true, 1.f)) > 20);
        processor->layoutMode.store(2); editor->resized();
        processor->custom.set("waveGain", .25f);
        check("Scope display gain changes measured geometry", changedPixels(baseline, waveformPixels()) > 100);
        processor->custom.set("waveGain", 1);
        std::array<Image,4> channelImages;
        for (int channels = 0; channels < 4; ++channels) {
            processor->custom.set("waveChannels", float(channels)); channelImages[std::size_t(channels)] = waveformPixels();
        }
        for (int channels = 1; channels < 4; ++channels)
            check("Scope channel view " + String(channels) + " differs from stereo", changedPixels(channelImages[0], channelImages[std::size_t(channels)]) > 100);
        check("Scope Left and Right show their distinct measured PCM", changedPixels(channelImages[1], channelImages[2]) > 100);
        processor->custom.set("waveChannels", 0);
        processor->custom.set("waveCustomTime", 1); processor->custom.set("waveSeconds", .75f);
        const auto historyFromScope = waveformPixels();
        check("Custom time overrides Scope rendering", changedPixels(baseline, historyFromScope) > 100);
        processor->waveformWindow.store(1);
        check("Custom-time history is independent of stored Scope selector", changedPixels(historyFromScope, waveformPixels()) == 0);
        processor->custom.set("waveCustomTime", 0); processor->waveformWindow.store(3);
    }
    void navigate(const String& section) {
        auto* button = dynamic_cast<TextButton*>(find(*panel, "nav" + section));
        check(section + " navigation exists", button != nullptr);
        if (button) button->triggerClick();
    }
    void scrollPanel(bool bottom) {
        if (auto* viewport = dynamic_cast<Viewport*>(find(*panel, "customizationViewport")))
            viewport->setViewPosition(0, bottom ? viewport->getViewedComponent()->getHeight() : 0);
        else check("Settings viewport exists", false);
    }
    void setSlider(const String& id, double value) {
        auto* slider = dynamic_cast<Slider*>(find(*panel, id + "Value"));
        check(id + " slider exists", slider != nullptr);
        if (slider) slider->setValue(value, sendNotificationSync);
    }
    void setChoice(const String& id, int value) {
        auto* choice = dynamic_cast<ComboBox*>(find(*panel, id + "Value"));
        check(id + " selector exists", choice != nullptr);
        if (choice) choice->setSelectedId(value + 1, sendNotificationSync);
    }
    void redrawEditor(int layout, int width = 1280, int height = 740) {
        processor->layoutMode.store(layout); editorWindow->setContentComponentSize(width, height); editor->resized(); editor->repaint();
    }
    static int desktopWindowCount() {
        int count = 0;
        auto& desktop = Desktop::getInstance();
        for (int i = 0; i < desktop.getNumComponents(); ++i)
            if (dynamic_cast<DocumentWindow*>(desktop.getComponent(i))) ++count;
        return count;
    }
    void selectAcrossTimerTick(ComboBox* choice, int selectedId, const String& label) {
        check(label + " selector exists", choice != nullptr);
        if (!choice) return;
        // Reproduce a normal asynchronous menu choice with a display refresh
        // already due. Older code overwrote the choice before onChange ran.
        // Stop this harness timer so the synchronous flush cannot re-enter it.
        stopTimer(); Thread::sleep(210);
        choice->setSelectedId(selectedId, sendNotificationAsync);
        Timer::callPendingTimersSynchronously();
        check(label + " keeps its pending menu selection through a timer tick", choice->getSelectedId() == selectedId);
        startTimerHz(30);
    }
    ScrollBar* dashboardScrollbar() const {
        for (auto* child : editor->getChildren())
            if (auto* scroll = dynamic_cast<ScrollBar*>(child)) return scroll;
        return nullptr;
    }
    void beginSceneExpansion() {
        sceneBaseline = {};
        sceneBaseline.appearance = processor->createAppearancePreset();
        sceneBaseline.layout = processor->layoutMode.load();
        sceneBaseline.editorBounds = editor->getBounds();
        sceneBaseline.windowBounds = editorWindow->getBounds();
        sceneBaseline.desktopWindows = desktopWindowCount();
        sceneBaseline.frozen = processor->frozen.load();
        sceneBaseline.meterGeneration = processor->analyzer.snapshot().resetGeneration;
        sceneBaseline.clip = find(*editor, "sceneClip");
        sceneBaseline.surface = find(*editor, "visualSurface");
        sceneBaseline.settingsOpen = editorSettings != nullptr && editorSettings->isVisible();
        if (auto* scroll = dashboardScrollbar()) sceneBaseline.scrollOffset = scroll->getCurrentRangeStart();
        if (auto* surface = sceneBaseline.surface.getComponent()) {
            sceneBaseline.parent = surface->getParentComponent();
            sceneBaseline.peer = surface->getPeer();
            sceneBaseline.context = OpenGLContext::getContextAttachedTo(*surface);
            if (sceneBaseline.context) sceneBaseline.nativeContext = sceneBaseline.context->getRawContext();
        }
        for (auto* child : editor->getChildren())
            if (child->getComponentID() != "expandScene" && (dynamic_cast<Button*>(child)
                || dynamic_cast<ComboBox*>(child) || dynamic_cast<Slider*>(child) || dynamic_cast<ScrollBar*>(child)))
                sceneBaseline.controls.emplace_back(Component::SafePointer<Component>(child), child->isVisible());
        auto* button = dynamic_cast<TextButton*>(find(*editor, "expandScene"));
        check("Scene expansion uses the actual visible editor button", button && button->isShowing());
        check("Scene expansion has its normal accessible title", button && button->getTitle() == "Expand visual");
        check("Scene expansion starts from persistent clip and GPU components", sceneBaseline.clip != nullptr && sceneBaseline.surface != nullptr);
        if (button) button->triggerClick();
    }
    void verifySceneIdentity(const String& label) {
        auto* clip = find(*editor, "sceneClip");
        auto* surface = find(*editor, "visualSurface");
        check(label + " retains the original scene components", clip && surface
            && clip == sceneBaseline.clip.getComponent() && surface == sceneBaseline.surface.getComponent());
        check(label + " keeps the GPU surface parent and native peer", surface
            && surface->getParentComponent() == sceneBaseline.parent.getComponent()
            && surface->getPeer() == sceneBaseline.peer && surface->getPeer() == editor->getPeer());
        check(label + " keeps the native GPU component visible", surface && surface->isVisible() && surface->isShowing());
        check(label + " creates no additional native window", desktopWindowCount() == sceneBaseline.desktopWindows);
        check(label + " preserves all appearance settings", processor->createAppearancePreset() == sceneBaseline.appearance);
        check(label + " preserves the previous meter layout", processor->layoutMode.load() == sceneBaseline.layout);
        check(label + " preserves display freeze and meter history", processor->frozen.load() == sceneBaseline.frozen
            && processor->analyzer.snapshot().resetGeneration == sceneBaseline.meterGeneration
            && processor->analyzer.waveformSnapshot().totalSamples == 32 * 48000);
        if (surface && sceneBaseline.context) {
            auto* current = OpenGLContext::getContextAttachedTo(*surface);
            check(label + " retains the attached OpenGL context", current == sceneBaseline.context);
            if (sceneBaseline.nativeContext) {
                check(label + " retains the underlying native OpenGL context", current && current->getRawContext() == sceneBaseline.nativeContext);
                ++nativeContextComparisons;
            }
        }
    }
    void verifySceneExpanded(const String& label) {
        verifySceneIdentity(label);
        auto* clip = find(*editor, "sceneClip");
        auto* surface = find(*editor, "visualSurface");
        auto* button = dynamic_cast<TextButton*>(find(*editor, "expandScene"));
        check(label + " stores expanded view state", processor->sceneExpanded.load());
        check(label + " fills the editor below its compact toolbar", clip
            && clip->getBounds() == Rectangle<int>(0, 40, editor->getWidth(), editor->getHeight() - 40));
        check(label + " removes the normal scene inset", clip && surface && surface->getBounds() == clip->getLocalBounds());
        check(label + " exposes an accessible restore button inside the toolbar", button && button->isShowing()
            && button->getTitle() == "Restore meters" && Rectangle<int>(0, 0, editor->getWidth(), 40).contains(button->getBounds()));
        bool controlsHidden = true;
        for (const auto& control : sceneBaseline.controls)
            if (control.first != nullptr && control.first->isVisible()) controlsHidden = false;
        check(label + " hides meter selectors, palette controls and dashboard scrolling", controlsHidden);
        check(label + " closes the embedded Customise dropdown", editorSettings == nullptr || !editorSettings->isVisible());
        check(label + " preserves the existing editor dimensions", editor->getBounds() == sceneBaseline.editorBounds);
        check(label + " preserves the existing native window dimensions", editorWindow->getBounds() == sceneBaseline.windowBounds);
    }
    void restoreScene(bool escape) {
        if (escape) check("Escape is handled while the visual is expanded", editor->keyPressed(KeyPress(KeyPress::escapeKey)));
        else {
            auto* button = dynamic_cast<TextButton*>(find(*editor, "expandScene"));
            check("Restore uses the actual visible scene button", button && button->isShowing());
            if (button) button->triggerClick();
        }
    }
    void verifySceneRestored(const String& label) {
        verifySceneIdentity(label);
        auto* clip = find(*editor, "sceneClip");
        auto* button = dynamic_cast<TextButton*>(find(*editor, "expandScene"));
        check(label + " clears expanded view state", !processor->sceneExpanded.load());
        check(label + " restores normal meter bounds", clip
            && clip->getBounds() == Rectangle<int>(16, 112, editor->getWidth() - 32, editor->getHeight() - 128));
        check(label + " restores the expansion button title", button && button->isShowing() && button->getTitle() == "Expand visual");
        check(label + " preserves the current editor dimensions", editor->getBounds() == sceneBaseline.editorBounds);
        check(label + " preserves the current native window dimensions", editorWindow->getBounds() == sceneBaseline.windowBounds);
        if (!sceneBaseline.settingsOpen) {
            bool visibilityRestored = true;
            for (const auto& control : sceneBaseline.controls)
                if (control.first == nullptr || control.first->isVisible() != control.second) visibilityRestored = false;
            check(label + " restores each control's previous visibility", visibilityRestored);
        }
        if (sceneBaseline.layout == 3) {
            auto* scroll = dashboardScrollbar();
            check(label + " restores the Custom layout scroll position", scroll
                && std::abs(scroll->getCurrentRangeStart() - sceneBaseline.scrollOffset) < .001);
        }
        check(label + " leaves Customise closed after returning to meters", editorSettings == nullptr || !editorSettings->isVisible());
    }
    void addSceneExpansionSteps() {
        for (const int mode : {0, 1, 2, 3}) {
            const String label = "scene-layout-" + String(mode);
            steps.push_back({label + "-prepare", [this, mode] {
                Process::makeForegroundProcess();
                settingsWindow->setVisible(false); editorWindow->toFront(true);
                processor->frozen.store(true);
                if (mode == 3) {
                    processor->custom.set("layoutColumns", 1);
                    processor->custom.set("orderWaveform", 1);
                    for (const char* key : {"showScene", "showSpectrum", "showSpectrogram", "showWaveform", "showStereo", "showLevels"})
                        processor->custom.set(key, 1);
                }
                redrawEditor(mode, 1280, mode == 1 ? 390 : mode == 2 ? 570 : 820);
            }, false, false, [this, mode] {
                if (mode == 3) {
                    auto* scroll = dashboardScrollbar();
                    check("Expansion fixture has a scrollable Custom dashboard", scroll && scroll->isVisible());
                    if (scroll) scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), sendNotificationSync);
                    check("Custom expansion starts from a nonzero scroll position", scroll && scroll->getCurrentRangeStart() > 0);
                }
            } });
            steps.push_back({label + "-expanded", [this] { beginSceneExpansion(); }, false, true,
                [this, label] { verifySceneExpanded(label); } });
            steps.push_back({label + "-restored", [this, mode] { restoreScene(mode % 2 != 0); }, false, true,
                [this, label] { verifySceneRestored(label); } });
        }
        steps.push_back({"scene-customise-prepare", [this] {
            redrawEditor(0, 1280, 820);
            if (auto* button = dynamic_cast<TextButton*>(findTitle(*editor, "Customise Aurora"))) button->triggerClick();
        }, false, false, [this] {
            check("Expansion fixture starts with Customise open", editorSettings != nullptr && editorSettings->isShowing());
        } });
        steps.push_back({"scene-customise-expanded", [this] { beginSceneExpansion(); }, false, true,
            [this] { verifySceneExpanded("Expanded from Customise"); } });
        steps.push_back({"scene-expanded-resize", [this] {
            editorWindow->setContentComponentSize(1472, 900);
            // An explicit user resize should survive Restore, rather than
            // restoring a stale size saved before entering the expanded view.
            sceneBaseline.editorBounds = editor->getBounds();
            sceneBaseline.windowBounds = editorWindow->getBounds();
        }, false, true, [this] { verifySceneExpanded("Resized expanded scene"); } });
        steps.push_back({"scene-resized-escape", [this] { restoreScene(true); }, false, true, [this] {
            verifySceneRestored("Escape after expanded resize");
        } });
        // Capture the restored 1472x900 editor before cleaning up the fixture.
        // Resetting here in the previous step's verify callback captured a
        // different layout and an old tooltip before its next layout tick.
        steps.push_back({"scene-expansion-fixture-reset", [this] {
            processor->resetAppearance(); redrawEditor(2, 1280, 570);
            settingsWindow->setVisible(true); settingsWindow->toFront(true);
        } });
    }
    void addSteps() {
        for (const int mode : {1, 2, 0, 3, 2}) {
            steps.push_back({ "header-layout-" + String(mode), [this, mode] {
                selectAcrossTimerTick(dynamic_cast<ComboBox*>(findTitle(*editor, "Layout")), mode + 1, "Header layout " + String(mode));
            }, false, true, [this, mode] {
                auto* choice = dynamic_cast<ComboBox*>(findTitle(*editor, "Layout"));
                check("Header layout " + String(mode) + " commits after asynchronous delivery",
                    processor->layoutMode.load() == mode && choice && choice->getSelectedId() == mode + 1);
                check("Header layout " + String(mode) + " resizes its editor", editor->getHeight() == (mode == 1 ? 390 : mode == 2 ? 570 : 820));
            } });
        }
        steps.push_back({ "editor-customize-open", [this] {
            desktopWindowsBeforeCustomise = desktopWindowCount();
            auto* button = dynamic_cast<TextButton*>(findTitle(*editor, "Customise Aurora"));
            check("Actual editor Customise button exists", button != nullptr);
            if (button) button->triggerClick();
        }, false, false, [this] {
            editorSettings = dynamic_cast<Viewport*>(find(*editor, "customizationDropdown"));
            check("Editor Customise opens an embedded dropdown", editorSettings != nullptr && editorSettings->isShowing());
            check("Customise creates no additional desktop window", desktopWindowCount() == desktopWindowsBeforeCustomise);
            if (editorSettings != nullptr) {
                check("Customise stays inside the plugin's native peer", editorSettings->getPeer() == editor->getPeer());
                check("Customise dropdown fits inside editor", editor->getLocalBounds().contains(editorSettings->getBounds()));
                capture(*editor, "editor-owned-customize-dropdown", false);
            }
        } });
        for (const int mode : {1, 2}) {
            steps.push_back({ "embedded-customize-layout-" + String(mode), [this, mode] {
                selectAcrossTimerTick(dynamic_cast<ComboBox*>(findTitle(*editor, "Layout")), mode + 1, "Open dropdown layout " + String(mode));
            }, false, true, [this, mode] {
                check("Layout changes while Customise is open", processor->layoutMode.load() == mode);
                check("Header layout selection reveals the chosen meters", editorSettings != nullptr && !editorSettings->isVisible());
            } });
            steps.push_back({ "embedded-customize-reopen-layout-" + String(mode), [this] {
                if (auto* button = dynamic_cast<TextButton*>(findTitle(*editor, "Customise Aurora"))) button->triggerClick();
            }, false, true, [this, mode] {
                check("Embedded dropdown stays within layout " + String(mode), editorSettings != nullptr
                    && editorSettings->isShowing() && editor->getLocalBounds().contains(editorSettings->getBounds()));
                if (editorSettings != nullptr) {
                    auto* content = editorSettings->getViewedComponent();
                    check("Small layouts preserve the full settings content", content && content->getHeight() >= 520);
                    auto* navigation = content ? find(*content, "navPresets") : nullptr;
                    if (navigation) {
                        const auto bounds = content->getLocalArea(navigation, navigation->getLocalBounds());
                        editorSettings->setViewPosition(0, bounds.getY());
                        check("Presets navigation is reachable by scrolling layout " + String(mode), editorSettings->getViewArea().contains(bounds));
                    } else check("Embedded Presets navigation exists", false);
                }
            } });
        }
        steps.push_back({ "embedded-layout-controls", [this] {
            if (auto* navigation = dynamic_cast<TextButton*>(find(*editor, "navLayout"))) navigation->triggerClick();
            else check("Embedded Layout navigation exists", false);
            if (editorSettings != nullptr) editorSettings->setViewPosition(0, 0);
        }, false, false });
        for (const int mode : {1, 2}) {
            steps.push_back({ "embedded-layout-control-" + String(mode), [this, mode] {
                selectAcrossTimerTick(dynamic_cast<ComboBox*>(find(*editor, "appearanceLayoutValue")), mode + 1, "Embedded layout " + String(mode));
            }, false, true, [this, mode] {
                check("Embedded layout " + String(mode) + " updates the actual editor", processor->layoutMode.load() == mode
                    && editor->getHeight() == (mode == 1 ? 390 : 570));
                check("Embedded Layout control keeps Customise open", editorSettings != nullptr && editorSettings->isShowing());
            } });
        }
        steps.push_back({ "editor-customize-close", [this] {
            if (auto* button = dynamic_cast<TextButton*>(findTitle(*editor, "Customise Aurora"))) button->triggerClick();
        }, false, false, [this] {
            check("Customise toggles closed without closing editor", editorSettings != nullptr && !editorSettings->isVisible() && editorWindow->isVisible());
            settingsWindow->toFront(true);
        } });
        steps.push_back({ "editor-customize-reopen", [this] {
            if (auto* button = dynamic_cast<TextButton*>(findTitle(*editor, "Customise Aurora"))) button->triggerClick();
        }, false, false, [this] {
            check("Customise reuses its embedded dropdown", editorSettings != nullptr && editorSettings->isShowing()
                && find(*editor, "customizationDropdown") == editorSettings.getComponent());
            check("Reopening Customise still creates no desktop window", desktopWindowCount() == desktopWindowsBeforeCustomise);
        } });
        steps.push_back({ "editor-customize-escape", [this] { editor->keyPressed(KeyPress(KeyPress::escapeKey)); }, false, false, [this] {
            check("Escape closes embedded Customise", editorSettings != nullptr && !editorSettings->isVisible());
        } });
        addSceneExpansionSteps();
        steps.push_back({ "colours-top", [this] { processor->frozen.store(true); }, true, false });
        steps.push_back({ "colours-module-overrides", [this] { scrollPanel(true); }, true, false });
        for (int i = 1; i < int(sections.size()); ++i)
            steps.push_back({ "tab-" + String(sections[std::size_t(i)]).toLowerCase(), [this, i] { navigate(sections[std::size_t(i)]); }, true, false });
        steps.push_back({ "presets-minimum-window", [this] {
            navigate("Presets"); settingsWindow->setContentComponentSize(500, 520);
        }, true, false, [this] {
            auto* status = find(*panel, "presetStatus");
            check("Preset status fits the minimum settings window", status && status->getParentComponent()->getLocalBounds().contains(status->getBounds()));
        } });
        steps.push_back({ "waveform-controls", [this] { settingsWindow->setContentComponentSize(560, 680); navigate("Waveform"); }, true, false });
        steps.push_back({ "waveform-live-edits", [this] {
            setChoice("appearanceWaveStyle", 1); setSlider("waveDensity", .35); setSlider("waveGain", 1.4);
            check("Native controls write waveform state", processor->waveformStyle.load() == 1
                && std::abs(processor->custom.get("waveDensity") - .35f) < .001f
                && std::abs(processor->custom.get("waveGain") - 1.4f) < .001f);
            scrollPanel(true);
        }, true, true });
        steps.push_back({ "waveform-colour-picker-open", [this] {
            // A CLI-launched QA app starts in the background on macOS. JUCE
            // dismisses native callouts after 200ms unless its app is active.
            Process::makeForegroundProcess(); settingsWindow->toFront(true);
            scrollPanel(false);
            auto* button = dynamic_cast<TextButton*>(find(*panel, "waveLowColourValue"));
            check("Waveform low-band colour button exists", button != nullptr);
            if (button) button->triggerClick();
        }, false, false, [this] {
            auto* hex = dynamic_cast<TextEditor*>(findDesktop("colourHex"));
            check("Native RGB picker opens its hex editor", hex != nullptr);
            if (hex) hex->setText("#13579B", true);
        } });
        steps.push_back({ "waveform-colour-picker-hex", [this] {
            check("Native hex input stores arbitrary exact RGB", processor->custom.get("waveLowColour") == float(0x13579b));
            if (auto* hex = dynamic_cast<TextEditor*>(findDesktop("colourHex")))
                if (auto* picker = hex->findParentComponentOfClass<CallOutBox>()) capture(*picker, "native-rgb-picker", false);
        }, false, false, [this] {
            if (auto* hex = dynamic_cast<TextEditor*>(findDesktop("colourHex")))
                if (auto* picker = hex->findParentComponentOfClass<CallOutBox>()) picker->dismiss();
        } });
        steps.push_back({ "scope-common-controls", [this] {
            processor->custom.resetSection(Customization::Section::Waveform);
            processor->waveformWindow.store(3); processor->frozen.store(true); redrawEditor(2, 1280, 570);
        }, false, true, [this] {
            verifyScopePixels();
            for (const auto* id : {"waveGrid", "waveLabels", "waveChannels", "waveGain", "waveFill", "waveCrest"}) {
                auto* control = find(*panel, String(id) + "Value");
                check(String("Scope common control remains applicable: ") + id, control && control->isEnabled());
            }
            auto* density = find(*panel, "waveDensityValue");
            check("Scrolling-only density is visibly disabled in Scope", density && !density->isEnabled());
        } });
        steps.push_back({ "panel-custom-time-enables-history", [this] {
            setToggle("waveCustomTime", true);
            check("Panel custom-time toggle leaves Scope for history", processor->custom.get("waveCustomTime") == 1 && processor->waveformWindow.load() == 1);
            setSlider("waveSeconds", .75);
        }, true, true, [this] {
            auto* density = find(*panel, "waveDensityValue");
            check("History material controls re-enable with custom time", density && density->isEnabled());
        } });
        steps.push_back({ "panel-explicit-window", [this] {
            setChoice("appearanceWaveWindow", 3);
            check("Panel explicit Scope choice disables custom time", processor->custom.get("waveCustomTime") == 0 && processor->waveformWindow.load() == 3);
            setToggle("waveCustomTime", true);
        }, false, false });
        steps.push_back({ "editor-explicit-window", [this] {
            auto* choice = dynamic_cast<ComboBox*>(findTitle(*editor, "Waveform time window"));
            check("Editor waveform time selector exists", choice != nullptr);
            if (choice) choice->setSelectedId(5, sendNotificationSync);
            check("Editor explicit time choice disables custom time", processor->custom.get("waveCustomTime") == 0 && processor->waveformWindow.load() == 4);
        }, false, true });
        steps.push_back({ "all-module-palettes", [this] {
            constexpr std::array<const char*, 6> prefixes { "wave", "spectrum", "spectrogram", "stereo", "levels", "scene" };
            constexpr std::array<std::array<uint32, 3>, 6> colours {{
                {{0x24424f,0x79d5ca,0xf1bbc8}}, {{0x4f263b,0xe88889,0xffe3b2}},
                {{0x282252,0x8b87df,0xcbe7f6}}, {{0x183b36,0x6fe9ab,0xf0e8a9}},
                {{0x533421,0xe8b76a,0xf5f0c8}}, {{0x1b264d,0x7ca3df,0xdfcbee}}
            }};
            for (std::size_t i = 0; i < prefixes.size(); ++i) {
                processor->custom.set((String(prefixes[i]) + "OwnPalette").toRawUTF8(), 1);
                for (std::size_t j = 0; j < 3; ++j)
                    processor->custom.set((String(prefixes[i]) + "Colour" + String::charToString(char('A' + j))).toRawUTF8(), float(colours[i][j]));
            }
            processor->waveformColour.store(1); processor->custom.set("paletteMiddle", .32f); redrawEditor(0);
        }, false, true });
        steps.push_back({ "ranges-and-interface", [this] {
            processor->custom.set("spectrumFloor", -72); processor->custom.set("spectrumCeiling", 6);
            processor->custom.set("spectrumMinHz", 40); processor->custom.set("spectrumMaxHz", 14000);
            processor->custom.set("spectrogramFloor", -78); processor->custom.set("spectrogramCeiling", 0);
            processor->custom.set("spectrogramContrast", 1.45f); processor->custom.set("levelsFloor", -72);
            processor->custom.set("levelsWarning", -9); processor->custom.set("interfaceOverride", 1);
            processor->custom.set("backgroundColour", float(0x161b2a)); processor->custom.set("panelColour", float(0x111627));
            processor->custom.set("textColour", float(0xe5ebf8)); processor->custom.set("gridColour", float(0x425269));
            redrawEditor(0);
        }, false, true });
        for (int channel = 0; channel < 4; ++channel)
            steps.push_back({ "waveform-channel-" + String(channel), [this, channel] {
                processor->custom.set("waveChannels", float(channel)); redrawEditor(2, 1280, 570);
            }, false, true });
        steps.push_back({ "layout-controls", [this] { navigate("Layout"); }, true, false });
        for (const int mode : {1, 2, 0}) {
            steps.push_back({ "panel-layout-" + String(mode), [this, mode] {
                selectAcrossTimerTick(dynamic_cast<ComboBox*>(find(*panel, "appearanceLayoutValue")), mode + 1, "Settings layout " + String(mode));
            }, true, true, [this, mode] {
                auto* choice = dynamic_cast<ComboBox*>(find(*panel, "appearanceLayoutValue"));
                check("Settings layout " + String(mode) + " commits after asynchronous delivery",
                    processor->layoutMode.load() == mode && choice && choice->getSelectedId() == mode + 1);
            } });
        }
        steps.push_back({ "custom-three-columns", [this] {
            setSlider("layoutColumns", 3);
            check("Column edits automatically choose Custom layout", processor->layoutMode.load() == 3);
            editor->resized(); editor->repaint();
        }, false, true });
        steps.push_back({ "custom-two-columns", [this] { setSlider("layoutColumns", 2); redrawEditor(3, 1280, 740); }, false, true });
        steps.push_back({ "custom-one-column-top", [this] { setSlider("layoutColumns", 1); redrawEditor(3, 1100, 620); }, false, true });
        steps.push_back({ "custom-one-column-bottom", [this] {
            bool found = false;
            for (auto* child : editor->getChildren())
                if (auto* scroll = dynamic_cast<ScrollBar*>(child); scroll && scroll->isVisible()) {
                    scroll->setCurrentRangeStart(scroll->getMaximumRangeLimit(), sendNotificationSync); found = true;
                }
            check("One-column custom layout exposes a scrollbar", found);
        }, false, true });
        steps.push_back({ "custom-reordered", [this] {
            setSlider("orderWaveform", 1); setSlider("layoutColumns", 2);
            check("Module order edit is stored", processor->custom.get("orderWaveform") == 1);
            redrawEditor(3, 1280, 740); scrollPanel(true);
        }, true, true });
        steps.push_back({ "custom-hidden-modules", [this] {
            processor->custom.set("showScene", 0); processor->custom.set("showSpectrogram", 0);
            processor->custom.set("showStereo", 0); redrawEditor(3, 1280, 570);
        }, false, true });
        steps.push_back({ "restored-default", [this] { processor->resetAppearance(); redrawEditor(2, 1280, 570); navigate("Presets"); }, true, true });
    }
    void compositeScenePixels(Component& root, Image& destination, const String& name) {
        using namespace juce::gl;
        auto* surface = find(root, "visualSurface");
        auto* context = surface ? OpenGLContext::getContextAttachedTo(*surface) : nullptr;
        check(name + " has a native OpenGL surface for capture", surface && context && context->getRawContext());
        if (!surface || !context || !context->getRawContext()) return;
        const auto fullArea = root.getLocalArea(surface, surface->getLocalBounds());
        auto clippedArea = fullArea.getIntersection(root.getLocalBounds());
        for (auto* parent = surface->getParentComponent(); parent && parent != &root; parent = parent->getParentComponent())
            clippedArea = clippedArea.getIntersection(root.getLocalArea(parent, parent->getLocalBounds()));
        check(name + " GPU surface intersects its real editor clip", !clippedArea.isEmpty());
        if (clippedArea.isEmpty()) return;
        int width = 0, height = 0;
        GLenum beforeError = GL_NO_ERROR, readError = GL_NO_ERROR;
        std::vector<GLubyte> rgba;
        // Geometry stays on the message thread; framebuffer reads and restored
        // GL pack/buffer state stay on the production surface's own GL thread.
        context->executeOnGLThread([&](OpenGLContext& active) {
            beforeError = glGetError();
            GLint viewport[4] {}, previousFramebuffer = 0, previousPackBuffer = 0;
            GLint packAlignment = 0, packRowLength = 0, packSkipRows = 0, packSkipPixels = 0;
            glGetIntegerv(GL_VIEWPORT, viewport); width = viewport[2]; height = viewport[3];
            if (width < 1 || height < 1 || width > 8192 || height > 8192) return;
            glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousFramebuffer);
            glGetIntegerv(GL_PIXEL_PACK_BUFFER_BINDING, &previousPackBuffer);
            glGetIntegerv(GL_PACK_ALIGNMENT, &packAlignment); glGetIntegerv(GL_PACK_ROW_LENGTH, &packRowLength);
            glGetIntegerv(GL_PACK_SKIP_ROWS, &packSkipRows); glGetIntegerv(GL_PACK_SKIP_PIXELS, &packSkipPixels);
            glBindFramebuffer(GL_READ_FRAMEBUFFER, active.getFrameBufferID());
            GLint previousReadBuffer = 0; glGetIntegerv(GL_READ_BUFFER, &previousReadBuffer);
            // For macOS's double-buffered native view, FRONT is the completed
            // presented image. BACK after a swap has undefined contents.
            glReadBuffer(active.getFrameBufferID() == 0 ? GL_FRONT : GL_COLOR_ATTACHMENT0);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
            glPixelStorei(GL_PACK_ALIGNMENT, 1); glPixelStorei(GL_PACK_ROW_LENGTH, 0);
            glPixelStorei(GL_PACK_SKIP_ROWS, 0); glPixelStorei(GL_PACK_SKIP_PIXELS, 0);
            rgba.resize(std::size_t(width) * std::size_t(height) * 4);
            glFinish(); glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
            readError = glGetError();
            glPixelStorei(GL_PACK_ALIGNMENT, packAlignment); glPixelStorei(GL_PACK_ROW_LENGTH, packRowLength);
            glPixelStorei(GL_PACK_SKIP_ROWS, packSkipRows); glPixelStorei(GL_PACK_SKIP_PIXELS, packSkipPixels);
            glBindBuffer(GL_PIXEL_PACK_BUFFER, GLuint(previousPackBuffer));
            glReadBuffer(GLenum(previousReadBuffer)); glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previousFramebuffer));
        }, true);
        const bool validRead = beforeError == GL_NO_ERROR && readError == GL_NO_ERROR && width > 0 && height > 0
            && rgba.size() == std::size_t(width) * std::size_t(height) * 4;
        check(name + " reads native GPU pixels without GL errors", validRead);
        if (!validRead) return;
        Image pixels(Image::RGB, width, height, false);
        std::set<std::uint32_t> colours;
        {
            Image::BitmapData bitmap(pixels, Image::BitmapData::writeOnly);
            for (int y = 0; y < height; ++y) for (int x = 0; x < width; ++x) {
                const auto offset = (std::size_t(height - 1 - y) * std::size_t(width) + std::size_t(x)) * 4;
                const Colour colour(rgba[offset], rgba[offset + 1], rgba[offset + 2]);
                bitmap.setPixelColour(x, y, colour);
                if (x % 8 == 0 && y % 8 == 0) colours.insert(colour.getARGB());
            }
        }
        check(name + " contains rendered scene detail rather than a blank framebuffer", colours.size() >= 32);
        {
            Graphics composite(destination); composite.reduceClipRegion(clippedArea);
            composite.drawImage(pixels, fullArea.toFloat());
        }
        const auto file = outputDirectory.getChildFile(name + "-gpu.png"); file.deleteFile();
        bool saved = false;
        if (auto stream = file.createOutputStream()) { saved = PNGImageFormat().writeImageToStream(pixels, *stream); stream->flush(); }
        check(name + " native-resolution GPU PNG saved", saved);
        auto* record = new DynamicObject; record->setProperty("name", name); record->setProperty("file", file.getFullPathName());
        record->setProperty("width", width); record->setProperty("height", height); record->setProperty("sampledColours", int(colours.size()));
        record->setProperty("editorBounds", fullArea.toString()); record->setProperty("clipBounds", clippedArea.toString());
        record->setProperty("beforeGlError", int(beforeError)); record->setProperty("readGlError", int(readError));
        gpuCaptures.add(var(record));
    }
    void capture(Component& component, const String& name, bool settings) {
        auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.f);
        // Read only expanded scenes and visible Studio/Custom restores. Parked
        // surfaces in meter-only/scrolled layouts never block this runner.
        const bool captureGpu = !settings && &component == editor
            && (processor->sceneExpanded.load() || name == "scene-layout-0-restored-editor"
                || name == "scene-layout-3-restored-editor");
        const int gpuCapturesBefore = gpuCaptures.size();
        if (captureGpu) compositeScenePixels(component, image, name);
        const auto file = outputDirectory.getChildFile(name + ".png"); file.deleteFile();
        bool saved = false;
        if (auto stream = file.createOutputStream()) { saved = PNGImageFormat().writeImageToStream(image, *stream); stream->flush(); }
        check(name + " native PNG saved", image.isValid() && saved);
        bool controlsFit = true;
        Array<var> controls;
        for (auto* child : component.getChildren()) {
            if (!child->isVisible()) continue;
            const bool inside = !child->getBounds().isEmpty() && component.getLocalBounds().contains(child->getBounds());
            controlsFit &= inside;
            auto* control = new DynamicObject; control->setProperty("id", child->getComponentID());
            control->setProperty("title", child->getTitle()); control->setProperty("bounds", child->getBounds().toString());
            control->setProperty("insideParent", inside); controls.add(var(control));
        }
        check(name + " visible top-level controls fit", controlsFit);
        if (settings)
            if (auto* rows = find(component, "customizationRows"))
                for (auto* row : rows->getChildren()) for (auto* child : row->getChildren())
                    if (child->isVisible()) check(row->getComponentID() + " control fits its row", row->getLocalBounds().contains(child->getBounds()));
        auto* record = new DynamicObject; record->setProperty("name", name); record->setProperty("file", file.getFullPathName());
        record->setProperty("width", image.getWidth()); record->setProperty("height", image.getHeight());
        record->setProperty("controls", var(controls)); record->setProperty("settingsPanel", settings);
        record->setProperty("includesNativeGpuPixels", gpuCaptures.size() > gpuCapturesBefore);
        record->setProperty("layout", processor->layoutMode.load()); record->setProperty("elapsedSeconds", Time::getMillisecondCounterHiRes() * .001 - started);
        captures.add(var(record));
    }
    void timerCallback() override {
        const double now = Time::getMillisecondCounterHiRes() * .001;
        if (now < nextAt) return;
        if (index >= int(steps.size())) { finish(); return; }
        auto& step = steps[std::size_t(index)];
        if (!waitingForPaint) { step.configure(); waitingForPaint = true; nextAt = now + .3; return; }
        if (step.verify) step.verify();
        if (step.capturePanel) capture(*panel, step.name + "-settings", true);
        if (step.captureEditor) capture(*editor, step.name + "-editor", false);
        waitingForPaint = false; ++index; nextAt = Time::getMillisecondCounterHiRes() * .001 + .06;
    }
    void finish(const String& error = {}) {
        if (finished) return; finished = true; stopTimer();
        if (error.isNotEmpty()) check(error, false);
        check("Native controls notify the editor", changeNotifications >= 5);
        check("QA never delivers or modifies audio after prefill", processor->analyzer.waveformSnapshot().totalSamples == 32 * 48000);
        check("Expanded scenes and Studio/Custom restores contain native GPU captures", gpuCaptures.size() >= 8);
        auto* report = new DynamicObject; report->setProperty("passed", failures == 0); report->setProperty("failures", failures);
        report->setProperty("checks", var(checks)); report->setProperty("captures", var(captures));
        report->setProperty("audioDeviceOpened", false); report->setProperty("userPresetLibraryModified", false);
        report->setProperty("nativeOpenGLContextIdentityComparisons", nativeContextComparisons);
        report->setProperty("nativeGpuCaptureCount", gpuCaptures.size()); report->setProperty("gpuCaptures", var(gpuCaptures));
        report->setProperty("captureMethod", "Native JUCE snapshots of all settings sections and the real editor. Expanded scenes and Studio/Custom restores include direct readback of the production OpenGL surface's presented front buffer, vertically oriented and composited at its real clipped editor bounds. Separate GPU PNG files preserve native physical resolution; other snapshots exclude GPU pixels.");
        report->setProperty("sourceFixture", "32 seconds of original procedural stereo music, no audio output");
        report->setProperty("changeNotifications", changeNotifications);
        if (!outputDirectory.getChildFile("report.json").replaceWithText(JSON::toString(var(report), true))) ++failures;
        std::cout << "Customization native QA: " << failures << " failures, " << captures.size() << " captures\n";
        setApplicationReturnValue(failures ? 1 : 0); quit();
    }
};
} // namespace
START_JUCE_APPLICATION(CustomizationTestApp)
