#include "CustomizationPanel.h"
#include "PluginProcessor.h"
#include "WaveformMusic.h"
#include <array>
#include <functional>
#include <iostream>
#include <vector>

// Developer-only native settings/editor QA. No audio device, host, preset
// library writes, import dialogs or export dialogs are used by this runner.
namespace {
using namespace juce;
const File outputDirectory("/tmp/aurora-customization-qa");
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
    std::vector<Step> steps;
    Array<var> checks, captures;
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
    void capture(Component& component, const String& name, bool settings) {
        const auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.f);
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
        auto* report = new DynamicObject; report->setProperty("passed", failures == 0); report->setProperty("failures", failures);
        report->setProperty("checks", var(checks)); report->setProperty("captures", var(captures));
        report->setProperty("audioDeviceOpened", false); report->setProperty("userPresetLibraryModified", false);
        report->setProperty("captureMethod", "Native JUCE component snapshots of all nine settings sections and the real editor. Studio OpenGL content is not included in component snapshots; scene colours are configured but GPU pixels are not validated here.");
        report->setProperty("sourceFixture", "32 seconds of original procedural stereo music, no audio output");
        report->setProperty("changeNotifications", changeNotifications);
        if (!outputDirectory.getChildFile("report.json").replaceWithText(JSON::toString(var(report), true))) ++failures;
        std::cout << "Customization native QA: " << failures << " failures, " << captures.size() << " captures\n";
        setApplicationReturnValue(failures ? 1 : 0); quit();
    }
};
} // namespace
START_JUCE_APPLICATION(CustomizationTestApp)
