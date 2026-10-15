#include "CustomizationPanel.h"
#include "Customization.h"
#include "FactoryPresetBrowser.h"
#include "PluginProcessor.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

namespace {
using namespace juce;
Colour rgb(uint32 value) { return Colour(value | 0xff000000u); }
const Colour background = rgb(0x111c1b), sidebar = rgb(0x172522), control = rgb(0x1c2c28);
const Colour foreground = rgb(0xe6ebd8), muted = rgb(0x9baea4), mint = rgb(0x9ee0be), border = rgb(0x344740);
constexpr std::array<const char*, 9> sectionNames {
    "Colours", "Waveform", "Spectrum", "Spectrogram", "Stereo", "Levels", "Scene", "Layout", "Presets"
};

class PanelLook final : public LookAndFeel_V4 {
public:
    PanelLook() {
        setColour(Label::textColourId, foreground);
        setColour(ComboBox::textColourId, foreground);
        setColour(ComboBox::backgroundColourId, control);
        setColour(ComboBox::outlineColourId, border);
        setColour(ComboBox::arrowColourId, muted);
        setColour(PopupMenu::backgroundColourId, sidebar);
        setColour(PopupMenu::textColourId, foreground);
        setColour(PopupMenu::highlightedBackgroundColourId, border);
        setColour(PopupMenu::highlightedTextColourId, mint);
        setColour(TextButton::buttonColourId, control);
        setColour(TextButton::buttonOnColourId, rgb(0x294538));
        setColour(TextButton::textColourOffId, foreground);
        setColour(TextButton::textColourOnId, mint);
        setColour(ToggleButton::textColourId, foreground);
        setColour(ToggleButton::tickColourId, mint);
        setColour(ToggleButton::tickDisabledColourId, muted);
        setColour(Slider::thumbColourId, mint);
        setColour(Slider::trackColourId, mint);
        setColour(Slider::backgroundColourId, border);
        setColour(Slider::textBoxTextColourId, foreground);
        setColour(Slider::textBoxBackgroundColourId, control);
        setColour(Slider::textBoxOutlineColourId, border);
        setColour(Slider::textBoxHighlightColourId, border);
        setColour(TextEditor::textColourId, foreground);
        setColour(TextEditor::backgroundColourId, control);
        setColour(TextEditor::outlineColourId, border);
        setColour(TextEditor::focusedOutlineColourId, mint.withAlpha(.7f));
        setColour(TextEditor::highlightColourId, border);
        setColour(ScrollBar::thumbColourId, border.brighter(.3f));
        setColour(ColourSelector::backgroundColourId, background);
        setColour(ColourSelector::labelTextColourId, foreground);
    }
    Font getTextButtonFont(TextButton&, int) override { return Font(FontOptions(13.f)); }
    Font getComboBoxFont(ComboBox&) override { return Font(FontOptions(13.f)); }
    void drawButtonBackground(Graphics& g, Button& button, const Colour& colour, bool hover, bool down) override {
        auto r = button.getLocalBounds().toFloat().reduced(.5f);
        g.setColour(colour.withMultipliedBrightness(down ? 1.3f : hover ? 1.14f : 1.f)
            .withMultipliedAlpha(button.isEnabled() ? 1.f : .45f));
        g.fillRoundedRectangle(r, 5);
        g.setColour(border.withAlpha(button.hasKeyboardFocus(true) ? 1.f : .65f));
        g.drawRoundedRectangle(r, 5, .7f);
        if (button.getToggleState()) {
            g.setColour(mint);
            g.fillRoundedRectangle(1, 8, 2, r.getHeight() - 16, 1);
        }
    }
    void drawLinearSlider(Graphics& g, int x, int y, int width, int height, float position,
                          float minimum, float maximum, Slider::SliderStyle style, Slider& slider) override {
        if (style != Slider::LinearHorizontal) {
            LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, position, minimum, maximum, style, slider);
            return;
        }
        const float cy = y + height * .5f;
        g.setColour(border); g.fillRoundedRectangle(float(x), cy - 1.5f, float(width), 3, 1.5f);
        g.setColour(mint.withAlpha(.7f)); g.fillRoundedRectangle(float(x), cy - 1.5f, std::max(0.f, position - x), 3, 1.5f);
        g.setColour(mint); g.fillEllipse(position - 4.5f, cy - 4.5f, 9, 9);
        g.setColour(background); g.fillEllipse(position - 1.5f, cy - 1.5f, 3, 3);
    }
};

class ColourPicker final : public Component, private ChangeListener {
public:
    ColourPicker(Colour initial, std::function<void(Colour)> changed)
        : selector(ColourSelector::showColourspace | ColourSelector::showSliders), onChange(std::move(changed)) {
        setLookAndFeel(&look);
        selector.setCurrentColour(initial, dontSendNotification);
        selector.addChangeListener(this);
        addAndMakeVisible(selector);
        hex.setTitle("Hex RGB colour"); hex.setComponentID("colourHex");
        hex.setFont(Font(FontOptions("Menlo", 14.f, Font::plain)));
        hex.setInputRestrictions(7, "#0123456789abcdefABCDEF");
        hex.setJustification(Justification::centred);
        hex.setText("#" + initial.toDisplayString(false), false);
        hex.onTextChange = [this] {
            const auto value = hex.getText().trimCharactersAtStart("#");
            if (value.length() != 6) return;
            const auto colour = rgb(static_cast<uint32>(value.getHexValue32()));
            selector.setCurrentColour(colour, dontSendNotification);
            onChange(colour);
        };
        hex.onReturnKey = hex.onEscapeKey = [this] {
            if (auto* callout = findParentComponentOfClass<CallOutBox>()) callout->dismiss();
        };
        addAndMakeVisible(hex);
        setSize(312, 342);
    }
    ~ColourPicker() override { selector.removeChangeListener(this); setLookAndFeel(nullptr); }
    void resized() override { hex.setBounds(14, 12, getWidth() - 28, 32); selector.setBounds(0, 53, getWidth(), getHeight() - 53); }
    void paint(Graphics& g) override { g.fillAll(background); }
private:
    PanelLook look;
    ColourSelector selector;
    TextEditor hex;
    std::function<void(Colour)> onChange;
    void changeListenerCallback(ChangeBroadcaster*) override {
        const auto colour = selector.getCurrentColour().withAlpha(1.f);
        hex.setText("#" + colour.toDisplayString(false), false);
        onChange(colour);
    }
};

class SettingRow final : public Component, public SettableTooltipClient {
public:
    using Kind = Customization::Kind;
    SettingRow(String id, String label, Kind type, double minimum, double maximum, double step,
               String choices, std::function<double()> read, std::function<void(double)> write)
        : title(std::move(label)), kind(type), readValue(std::move(read)), writeValue(std::move(write)) {
        setComponentID(id); setTitle(title);
        if (kind == Kind::Slider) {
            slider.setSliderStyle(Slider::LinearHorizontal);
            slider.setTextBoxStyle(Slider::TextBoxRight, false, 70, 27);
            slider.setRange(minimum, maximum, step);
            slider.setNumDecimalPlacesToDisplay(step >= 1 ? 0 : step >= .1 ? 1 : 2);
            slider.setTitle(title); slider.setComponentID(id + "Value");
            slider.setDoubleClickReturnValue(true, readValue());
            slider.onValueChange = [this] { writeValue(slider.getValue()); };
            addAndMakeVisible(slider);
        } else if (kind == Kind::Choice) {
            choice.addItemList(StringArray::fromTokens(choices, "|", ""), 1);
            choice.setTitle(title); choice.setComponentID(id + "Value");
            choice.onChange = [this] { if (choice.getSelectedId() > 0) writeValue(choice.getSelectedId() - 1); };
            addAndMakeVisible(choice);
        } else if (kind == Kind::Toggle) {
            toggle.setTitle(title); toggle.setComponentID(id + "Value");
            toggle.setButtonText("On");
            toggle.onClick = [this] { writeValue(toggle.getToggleState() ? 1 : 0); sync(); };
            addAndMakeVisible(toggle);
        } else {
            colour.setTitle(title); colour.setComponentID(id + "Value");
            setHelpTip("Choose any RGB colour, or enter a six-digit hex value.");
            colour.onClick = [this] {
                Component::SafePointer<SettingRow> safe(this);
                auto picker = std::make_unique<ColourPicker>(rgb(static_cast<uint32>(readValue())), [safe](Colour value) {
                    if (safe != nullptr && safe->isEnabled()) {
                        safe->writeValue(value.getARGB() & 0x00ffffffu);
                        safe->sync();
                    }
                });
                CallOutBox::launchAsynchronously(std::move(picker), colour.getScreenBounds(), nullptr);
            };
            addAndMakeVisible(colour);
        }
        sync();
    }
    int rowHeight() const { return kind == Kind::Toggle || kind == Kind::Colour ? 49 : 72; }
    void setHelpTip(const String& tip) {
        helpTip = tip;
        applyTooltip(tip);
    }
    void setApplicable(bool applicable, const String& reason = {}) {
        setEnabled(applicable);
        setAlpha(applicable ? 1.f : .48f);
        applyTooltip(applicable ? helpTip : reason);
    }
    void applyTooltip(const String& tip) {
        setTooltip(tip);
        setDescription(tip);
        slider.setTooltip(tip); choice.setTooltip(tip); toggle.setTooltip(tip); colour.setTooltip(tip);
    }
    void sync() {
        const auto value = readValue();
        if (kind == Kind::Slider) {
            if (!slider.isMouseButtonDown() && !slider.hasKeyboardFocus(true)) slider.setValue(value, dontSendNotification);
        } else if (kind == Kind::Choice) {
            // ComboBox delivers menu selections asynchronously. Refresh only
            // after the stored value changes, so a timer cannot replace a
            // user's selection before its onChange callback commits it.
            if (!choiceSynced || value != lastChoiceValue) {
                choiceSynced = true; lastChoiceValue = value;
                choice.setSelectedId(int(value) + 1, dontSendNotification);
            }
        }
        else if (kind == Kind::Toggle) { toggle.setToggleState(value >= .5, dontSendNotification); toggle.setButtonText(value >= .5 ? "On" : "Off"); }
        else {
            const auto selected = rgb(static_cast<uint32>(value));
            colour.setButtonText("#" + selected.toDisplayString(false));
            colour.setColour(TextButton::buttonColourId, selected);
            colour.setColour(TextButton::textColourOffId, selected.contrasting(1.f));
        }
    }
    void resized() override {
        if (kind == Kind::Slider) slider.setBounds(0, 29, getWidth(), 30);
        else if (kind == Kind::Choice) choice.setBounds(0, 29, getWidth(), 30);
        else if (kind == Kind::Toggle) toggle.setBounds(getWidth() - 62, 7, 62, 31);
        else colour.setBounds(getWidth() - 116, 7, 116, 31);
    }
    void paint(Graphics& g) override {
        g.setColour(foreground); g.setFont(Font(FontOptions(13.f)));
        const int right = kind == Kind::Toggle ? 68 : kind == Kind::Colour ? 124 : 0;
        g.drawFittedText(title, 0, kind == Kind::Toggle || kind == Kind::Colour ? 12 : 2,
                        getWidth() - right, 23, Justification::centredLeft, 1);
        g.setColour(border.withAlpha(.35f)); g.drawHorizontalLine(getHeight() - 1, 0, float(getWidth()));
    }
private:
    String title, helpTip;
    Kind kind;
    std::function<double()> readValue;
    std::function<void(double)> writeValue;
    Slider slider;
    ComboBox choice;
    double lastChoiceValue = 0;
    bool choiceSynced = false;
    ToggleButton toggle;
    TextButton colour;
};

class CustomizationPanel final : public Component, private Timer {
public:
    CustomizationPanel(AuroraAudioProcessor& processor, std::function<void()> changed)
        : p(processor), onChange(std::move(changed)), presetDirectory(File::getSpecialLocation(File::userApplicationDataDirectory)
              .getChildFile("Aurora").getChildFile("Presets")) {
        setLookAndFeel(&look); setName("Aurora customization"); setComponentID("customizationPanel");
        for (std::size_t i = 0; i < navigation.size(); ++i) {
            auto& button = navigation[i];
            button.setButtonText(sectionNames[i]); button.setComponentID("nav" + String(sectionNames[i]));
            button.setTitle(String(sectionNames[i]) + " settings");
            button.setClickingTogglesState(true); button.setRadioGroupId(76143);
            button.onClick = [this, i] { selectSection(int(i)); };
            addAndMakeVisible(button);
        }
        viewport.setViewedComponent(&rowsContainer, false);
        viewport.setComponentID("customizationViewport"); rowsContainer.setComponentID("customizationRows");
        viewport.setScrollBarsShown(true, false); viewport.setScrollBarThickness(8);
        addAndMakeVisible(viewport);
        resetSection.setButtonText("Reset section"); resetSection.setComponentID("resetSection");
        resetSection.onClick = [this] { resetCurrentSection(); };
        addAndMakeVisible(resetSection);
        setupPresets();
        setSize(560, 680);
        selectSection(0);
        startTimerHz(6);
    }
    ~CustomizationPanel() override {
        stopTimer();
        fileChooser.reset();
        viewport.setViewedComponent(nullptr, false);
        factoryBrowser.reset();
        setLookAndFeel(nullptr);
    }
    void resized() override {
        for (std::size_t i = 0; i < navigation.size(); ++i)
            navigation[i].setBounds(12, 91 + int(i) * 41 + (i == 8 ? 17 : 0), 124, 34);
        resetSection.setBounds(getWidth() - 134, 31, 112, 28);
        viewport.setBounds(166, 105, getWidth() - 186, getHeight() - 127);
        layoutRows();
        factoryTab.setBounds(166, 99, (getWidth()-198)/2, 31);
        savedTab.setBounds(factoryTab.getRight()+10, 99, getWidth()-188-factoryTab.getWidth()-10, 31);
        presets.setBounds(166, 145, getWidth() - 188, getHeight() - 167);
        if(factoryBrowser)factoryBrowser->setBounds(166,145,getWidth()-184,getHeight()-164);
        layoutPresets();
    }
    void paint(Graphics& g) override {
        g.fillAll(background);
        g.setColour(sidebar); g.fillRect(0, 0, 148, getHeight());
        g.setColour(border.withAlpha(.6f)); g.drawVerticalLine(148, 0, float(getHeight()));
        g.setColour(foreground); g.setFont(Font(FontOptions(19.f)));
        g.drawText("Customise", 17, 30, 122, 25, Justification::centredLeft);
        g.setFont(Font(FontOptions(22.f))); g.drawText(sectionNames[std::size_t(section)], 166, 25, getWidth() - 315, 35, Justification::centredLeft);
        const auto colours = auroraPalette(p.palette.load(), p.customColourA.load(), p.customColourB.load(), p.customColourC.load());
        const bool reversed = p.custom.get("paletteReverse") >= .5f;
        const float left = 166, right = float(getWidth() - 23);
        ColourGradient gradient(rgb(colours[reversed ? 2 : 0]), left, 0, rgb(colours[reversed ? 0 : 2]), right, 0, false);
        gradient.addColour(jlimit(.1f, .9f, p.custom.get("paletteMiddle")), rgb(colours[1]));
        g.setGradientFill(gradient); g.fillRoundedRectangle(left, 76, right - left, 4, 2);
    }
private:
    PanelLook look;
    AuroraAudioProcessor& p;
    std::function<void()> onChange;
    TooltipWindow tips { this, 550 };
    std::array<TextButton, 9> navigation;
    Viewport viewport;
    Component rowsContainer, presets;
    std::unique_ptr<Component> factoryBrowser;
    TextButton factoryTab{"Collection"}, savedTab{"Saved"};
    bool factorySelected=true;
    std::vector<std::unique_ptr<SettingRow>> rows;
    TextButton resetSection, loadPreset { "Load" }, savePreset { "Save as new" }, replacePreset { "Replace selected" };
    TextButton importPreset { "Import" }, exportPreset { "Export" }, resetAll { "Reset appearance" };
    ComboBox library;
    TextEditor presetName;
    Label libraryLabel, nameLabel, status;
    File presetDirectory;
    std::vector<File> presetFiles;
    std::unique_ptr<FileChooser> fileChooser;
    bool chooserActive = false;
    int section = 0;

    void syncRows() {
        const bool customTime = p.custom.get("waveCustomTime") >= .5f;
        const bool scope = p.waveformWindow.load() == 3 && !customTime;
        const int layout = p.layoutMode.load();
        for (auto& row : rows) {
            const auto id = row->getComponentID();
            const bool scrollingOnly = id == "appearanceWaveStyle" || id == "appearanceWaveColour"
                || id == "appearanceWaveZoom" || id == "waveDensity" || id == "waveCore"
                || id == "waveBands" || id == "waveGradient" || id == "waveLowColour"
                || id == "waveMidColour" || id == "waveHighColour";
            if (scope && scrollingOnly)
                row->setApplicable(false, "Used by the scrolling waveform. Choose a time window or enable Custom time window to use this control.");
            else if (id == "waveSeconds" && !customTime)
                row->setApplicable(false, "Enable Custom time window to choose the number of seconds.");
            else if ((id == "layoutSceneWidth" || id == "layoutTopHeight") && layout != 0)
                row->setApplicable(false, "Used by the Studio layout. Choose Studio above to adjust this proportion.");
            else if (id == "layoutLevelWidth" && layout != 0 && layout != 2)
                row->setApplicable(false, "Used by Studio and Waveform layouts. Choose either layout above to adjust the levels width.");
            else row->setApplicable(true);
            row->sync();
        }
    }
    void changed() { syncRows(); if (onChange) onChange(); repaint(); }
    void timerCallback() override { if (isShowing()) { syncRows(); repaint(); } }
    void layoutRows() {
        const int width = jmax(80, viewport.getWidth() - 15);
        int y = 0;
        for (auto& row : rows) { row->setBounds(0, y, width, row->rowHeight()); y += row->rowHeight() + 7; }
        rowsContainer.setSize(width, jmax(viewport.getHeight(), y));
    }
    void addRow(const String& id, const String& label, Customization::Kind kind, double min, double max, double step,
                const String& choices, std::function<double()> read, std::function<void(double)> write) {
        auto row = std::make_unique<SettingRow>(id, label, kind, min, max, step, choices, std::move(read),
            [this, id, write = std::move(write)](double value) {
                write(value);
                if (id == "appearanceWaveWindow") p.custom.set("waveCustomTime", 0);
                if (id == "waveCustomTime" && value >= .5 && p.waveformWindow.load() == 3) p.waveformWindow.store(1);
                if (id == "layoutColumns" || id.startsWith("show") || id.startsWith("order")) p.layoutMode.store(3);
                if (id == "backgroundColour" || id == "panelColour" || id == "textColour" || id == "gridColour")
                    p.custom.set("interfaceOverride", 1);
                for (const auto* prefix : { "wave", "spectrum", "spectrogram", "stereo", "levels", "scene" }) {
                    const String module(prefix);
                    if (id.startsWith(module + "Colour")) {
                        p.custom.set((module + "OwnPalette").toRawUTF8(), 1);
                        if (module == "wave") p.waveformColour.store(1);
                    }
                }
                if (id == "waveOwnPalette" && value >= .5) p.waveformColour.store(1);
                if (id == "waveLowColour" || id == "waveMidColour" || id == "waveHighColour") p.waveformColour.store(0);
                changed();
            });
        if (id == "layoutColumns" || id.startsWith("show") || id.startsWith("order")) row->setHelpTip("Uses the Custom layout. Changing this control switches to Custom automatically.");
        else if (id == "layoutSceneWidth" || id == "layoutTopHeight") row->setHelpTip("Adjusts the Studio layout. Choose Studio above to see this proportion.");
        else if (id == "layoutLevelWidth") row->setHelpTip("Adjusts the levels panel width in Studio and Waveform layouts.");
        else if (id == "paletteMiddle") row->setHelpTip("Moves the colour gradient midpoint in the meters.");
        else if (id == "waveGain") row->setHelpTip("Display magnification only. Scrolling waveform: multiplies amplitude zoom. Scope: scales the trace. Measurements and audio stay unchanged.");
        else if (id == "waveLowColour" || id == "waveMidColour" || id == "waveHighColour") row->setHelpTip("Frequency colour used in Bands mode. Choose any RGB or six-digit hex colour.");
        rowsContainer.addAndMakeVisible(*row); rows.push_back(std::move(row));
    }
    void atomicChoice(const String& id, const String& label, std::atomic<int>& value, const String& choices) {
        addRow(id, label, Customization::Kind::Choice, 0, 0, 1, choices,
            [&value] { return double(value.load()); }, [&value](double next) { value.store(int(next)); });
    }
    void selectSection(int selected) {
        section = jlimit(0, 8, selected);
        for (std::size_t i = 0; i < navigation.size(); ++i) navigation[i].setToggleState(int(i) == section, dontSendNotification);
        const bool isPresets = section == 8;
        viewport.setVisible(!isPresets); presets.setVisible(isPresets && !factorySelected); resetSection.setVisible(!isPresets);
        factoryTab.setVisible(isPresets);savedTab.setVisible(isPresets);
        if(factoryBrowser)factoryBrowser->setVisible(isPresets && factorySelected);
        rows.clear();
        if (isPresets) { refreshLibrary(); repaint(); return; }
        using Kind = Customization::Kind;
        if (section == 0) {
            atomicChoice("appearanceTheme", "Theme", p.themeMode, "Soft Club|Afterhours|Chrome|Flight Deck");
            StringArray names; for (auto* name : auroraPaletteNames) names.add(name);
            atomicChoice("appearancePalette", "Palette", p.palette, names.joinIntoString("|"));
            const std::array<const char*, 3> labels { "Gradient start", "Gradient middle", "Gradient end" };
            const std::array<std::atomic<uint32>*, 3> stops { &p.customColourA, &p.customColourB, &p.customColourC };
            for (std::size_t i = 0; i < stops.size(); ++i)
                addRow("gradientStop" + String(int(i)), labels[i], Kind::Colour, 0, 16777215, 1, {},
                    [this, i] { return double(auroraPalette(p.palette.load(), p.customColourA.load(), p.customColourB.load(), p.customColourC.load())[i]); },
                    [this, i, stops](double value) {
                        const auto current = auroraPalette(p.palette.load(), p.customColourA.load(), p.customColourB.load(), p.customColourC.load());
                        for (std::size_t j = 0; j < stops.size(); ++j) stops[j]->store(j == i ? uint32(value) : current[j]);
                        p.palette.store(auroraCustomPalette);
                    });
        } else if (section == 1) {
            atomicChoice("appearanceWaveStyle", "Material", p.waveformStyle, "Silk|Prism|Trace");
            atomicChoice("appearanceWaveWindow", "Time window", p.waveformWindow, "6 s|12 s|24 s|Scope|3 s|1 s");
            atomicChoice("appearanceWaveColour", "Colour mode", p.waveformColour, "Bands|Palette");
            atomicChoice("appearanceWaveZoom", "Amplitude zoom", p.waveformZoom, "1x|2x|4x");
        } else if (section == 6) {
            atomicChoice("appearanceScene", "Scene", p.visualMode, "Folded Light|Liquid Aperture|Pearl Cloud|Night Transit|Diffraction Veil|Orbit Loom");
            addRow("appearanceIntensity", "Visual intensity", Kind::Slider, .25, 4, .05, {},
                [this] { return p.visualGain.load(); }, [this](double value) { p.visualGain.store(float(value)); });
            addRow("appearanceCameraYaw", "Camera yaw (degrees)", Kind::Slider, -180, 180, 1, {},
                [this] { return radiansToDegrees(double(p.cameraYaw.load())); },
                [this](double value) { p.cameraYaw.store(float(degreesToRadians(value))); });
            addRow("appearanceCameraPitch", "Camera pitch (degrees)", Kind::Slider, -180, 180, 1, {},
                [this] { return radiansToDegrees(double(p.cameraPitch.load())); },
                [this](double value) { p.cameraPitch.store(float(degreesToRadians(value))); });
            addRow("appearanceCameraZoom", "Camera zoom", Kind::Slider, .55, 2.4, .01, {},
                [this] { return p.cameraZoom.load(); }, [this](double value) { p.cameraZoom.store(float(value)); });
        } else if (section == 7) {
            atomicChoice("appearanceLayout", "Layout", p.layoutMode, "Studio|Meter strip|Waveform|Custom");
        }
        for (const auto& descriptor : Customization::descriptors) {
            if (int(descriptor.section) != section) continue;
            const auto* id = descriptor.id;
            addRow(id, descriptor.label, descriptor.kind, descriptor.min, descriptor.max, descriptor.step,
                descriptor.choices, [this, id] { return p.custom.get(id); },
                [this, id](double value) { p.custom.set(id, float(value)); });
        }
        syncRows(); layoutRows(); viewport.setViewPosition(0, 0); repaint();
    }
    void resetCurrentSection() {
        if (section >= 8) return;
        p.custom.resetSection(static_cast<Customization::Section>(section));
        switch (section) {
            case 0: p.themeMode.store(auroraDefaultTheme); p.palette.store(auroraDefaultPalette);
                    p.customColourA.store(0x214e68); p.customColourB.store(0x82bac7); p.customColourC.store(0xe4f2e9); break;
            case 1: p.waveformStyle.store(0); p.waveformWindow.store(1); p.waveformColour.store(0); p.waveformZoom.store(0); break;
            case 6: p.visualMode.store(2); p.visualGain.store(1);
                    p.cameraYaw.store(0); p.cameraPitch.store(0); p.cameraZoom.store(1); break;
            case 7: p.layoutMode.store(0); break;
            default: break;
        }
        changed();
    }
    void setStatus(const String& message, bool error = false) {
        status.setColour(Label::textColourId, error ? rgb(0xf0a08d) : mint);
        status.setText(message, dontSendNotification);
    }
    void setupPresets() {
        addChildComponent(presets);
        factoryBrowser=createFactoryPresetBrowser(p,[this]{changed();});
        factoryBrowser->setComponentID("factoryPresetBrowser");addChildComponent(*factoryBrowser);
        factoryTab.setComponentID("factoryPresetTab");savedTab.setComponentID("savedPresetTab");
        for(auto* button:{&factoryTab,&savedTab}){button->setClickingTogglesState(true);button->setRadioGroupId(76144);addChildComponent(button);}
        factoryTab.setToggleState(true,dontSendNotification);
        factoryTab.onClick=[this]{factorySelected=true;presets.setVisible(false);factoryBrowser->setVisible(true);};
        savedTab.onClick=[this]{factorySelected=false;factoryBrowser->setVisible(false);presets.setVisible(true);refreshLibrary();};
        libraryLabel.setText("Saved presets", dontSendNotification); nameLabel.setText("Preset name", dontSendNotification);
        for (auto* label : { &libraryLabel, &nameLabel, &status }) {
            label->setFont(Font(FontOptions(13.f))); label->setJustificationType(Justification::topLeft);
            presets.addAndMakeVisible(label);
        }
        library.setTextWhenNothingSelected("Choose a preset"); library.setComponentID("presetLibrary");
        library.onChange = [this] {
            const bool selected = library.getSelectedId() > 0;
            loadPreset.setEnabled(selected); replacePreset.setEnabled(selected);
            if (selected) presetName.setText(library.getText(), false);
        };
        presetName.setTitle("Preset name"); presetName.setComponentID("presetName");
        presetName.setTextToShowWhenEmpty("Name your preset", muted); presetName.setInputRestrictions(72);
        for (Component* item : std::initializer_list<Component*> { &library, &presetName, &loadPreset, &savePreset,
                 &replacePreset, &importPreset, &exportPreset, &resetAll }) presets.addAndMakeVisible(item);
        loadPreset.setComponentID("loadPreset"); savePreset.setComponentID("savePreset");
        replacePreset.setComponentID("replacePreset"); importPreset.setComponentID("importPreset");
        exportPreset.setComponentID("exportPreset"); resetAll.setComponentID("resetAppearance");
        loadPreset.onClick = [this] { if (auto file = selectedFile(); file.existsAsFile()) loadFile(file); };
        savePreset.onClick = [this] { saveLocal(false); };
        replacePreset.onClick = [this] { saveLocal(true); };
        importPreset.onClick = [this] { chooseFile(false); };
        exportPreset.onClick = [this] { chooseFile(true); };
        resetAll.onClick = [this] { p.resetAppearance(); changed(); setStatus("Appearance reset."); };
        status.setComponentID("presetStatus");
    }
    void layoutPresets() {
        const int width = presets.getWidth(), gap = 10, half = (width - gap) / 2;
        const bool compact = presets.getHeight() < 450;
        libraryLabel.setBounds(0, 4, width, 23);
        library.setBounds(0, 33, width - 77, 32); loadPreset.setBounds(width - 67, 33, 67, 32);
        nameLabel.setBounds(0, compact ? 80 : 91, width, 23); presetName.setBounds(0, compact ? 109 : 120, width, 33);
        savePreset.setBounds(0, compact ? 154 : 166, half, 33); replacePreset.setBounds(half + gap, compact ? 154 : 166, width - half - gap, 33);
        importPreset.setBounds(0, compact ? 211 : 237, half, 33); exportPreset.setBounds(half + gap, compact ? 211 : 237, width - half - gap, 33);
        resetAll.setBounds(0, compact ? 266 : 313, width, 33);
        const int statusTop = compact ? 311 : 372;
        status.setBounds(0, statusTop, width, jmax(0, presets.getHeight() - statusTop));
    }
    File selectedFile() const {
        const auto index = library.getSelectedId() - 1;
        return index >= 0 && index < int(presetFiles.size()) ? presetFiles[std::size_t(index)] : File();
    }
    void refreshLibrary(const File& select = {}) {
        const auto previous = select == File() ? selectedFile() : select;
        presetFiles.clear(); library.clear(dontSendNotification);
        for (const auto& file : presetDirectory.findChildFiles(File::findFiles, false, "*.aurora")) presetFiles.push_back(file);
        std::sort(presetFiles.begin(), presetFiles.end(), [](const File& a, const File& b) { return a.getFileName().compareNatural(b.getFileName()) < 0; });
        for (std::size_t i = 0; i < presetFiles.size(); ++i) {
            library.addItem(presetFiles[i].getFileName().dropLastCharacters(7), int(i) + 1);
            if (presetFiles[i] == previous) library.setSelectedId(int(i) + 1, dontSendNotification);
        }
        loadPreset.setEnabled(library.getSelectedId() > 0); replacePreset.setEnabled(library.getSelectedId() > 0);
        library.setTextWhenNothingSelected(presetFiles.empty() ? "No saved presets" : "Choose a preset");
    }
    void saveLocal(bool replace) {
        const auto name = File::createLegalFileName(presetName.getText().trim()).trimCharactersAtEnd(". ");
        if (name.isEmpty()) { setStatus("Enter a preset name first.", true); return; }
        if (!presetDirectory.createDirectory().wasOk()) { setStatus("The preset folder could not be created.", true); return; }
        const auto file = replace ? selectedFile() : presetDirectory.getNonexistentChildFile(name, ".aurora", true);
        if (file == File() || !file.replaceWithText(p.createAppearancePreset())) { setStatus("The preset could not be saved.", true); return; }
        refreshLibrary(file);
        setStatus(replace ? "Selected preset replaced." : "Saved as a new preset.");
    }
    void loadFile(const File& file) {
        if (!file.existsAsFile() || file.getSize() > 256 * 1024 || !p.applyAppearancePreset(file.loadFileAsString())) {
            setStatus("This file is not a valid Aurora appearance preset.", true); return;
        }
        changed(); setStatus("Loaded " + file.getFileNameWithoutExtension() + ".");
    }
    void chooseFile(bool save) {
        if (chooserActive) return;
        chooserActive = true;
        fileChooser = std::make_unique<FileChooser>(save ? "Export Aurora appearance" : "Import Aurora appearance",
            save ? presetDirectory.getChildFile("Aurora.aurora") : presetDirectory, "*.aurora;*.xml");
        Component::SafePointer<CustomizationPanel> safe(this);
        const int flags = FileBrowserComponent::canSelectFiles | (save
            ? FileBrowserComponent::saveMode | FileBrowserComponent::warnAboutOverwriting
            : FileBrowserComponent::openMode);
        fileChooser->launchAsync(flags, [safe, save](const FileChooser& chooser) {
            if (safe == nullptr) return;
            safe->chooserActive = false;
            auto file = chooser.getResult(); if (file == File()) return;
            if (save) {
                // Keep the exact path approved by the native save dialog; an
                // extension rewrite could bypass its overwrite confirmation.
                const bool written = file.replaceWithText(safe->p.createAppearancePreset());
                safe->setStatus(written ? "Appearance exported." : "The export could not be written.", !written);
            } else safe->loadFile(file);
        });
    }
};
} // namespace

std::unique_ptr<juce::Component> createCustomizationPanel(AuroraAudioProcessor& processor, std::function<void()> onChange) {
    return std::make_unique<CustomizationPanel>(processor, std::move(onChange));
}
