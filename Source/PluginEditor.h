#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "VisualSurface.h"
#include "WaveformClock.h"
#include "WaveformProjection.h"
#include "WaveformRibbon.h"
#include <deque>

class SoftClubLookAndFeel final : public juce::LookAndFeel_V4 {
public:
    SoftClubLookAndFeel();
    void drawButtonBackground(juce::Graphics&, juce::Button&, const juce::Colour&, bool, bool) override;
    void drawButtonText(juce::Graphics&, juce::TextButton&, bool, bool) override;
    void drawComboBox(juce::Graphics&, int, int, bool, int, int, int, int, juce::ComboBox&) override;
    juce::Font getComboBoxFont(juce::ComboBox&) override;
    juce::Font getTextButtonFont(juce::TextButton&, int) override;
    void drawPopupMenuItem(juce::Graphics&, const juce::Rectangle<int>&, bool, bool, bool, bool, bool,
                           const juce::String&, const juce::String&, const juce::Drawable*, const juce::Colour*) override;
    void getIdealPopupMenuItemSize(const juce::String&, bool, int, int&, int&) override;
    void drawLinearSlider(juce::Graphics&, int, int, int, int, float, float, float,
                          juce::Slider::SliderStyle, juce::Slider&) override;
    void drawPopupMenuItemWithOptions(juce::Graphics&, const juce::Rectangle<int>&, bool,
                                     const juce::PopupMenu::Item&, const juce::PopupMenu::Options&) override;
    void getIdealPopupMenuItemSizeWithOptions(const juce::String&, bool, int, int&, int&,
                                              const juce::PopupMenu::Options&) override;
    bool instrumentStyle = false;
private:
    bool factoryMenuContext = false;
};

class AuroraAudioProcessorEditor final : public juce::AudioProcessorEditor, public juce::TooltipClient, private juce::Timer, private juce::ScrollBar::Listener {
public:
    explicit AuroraAudioProcessorEditor(AuroraAudioProcessor&);
    ~AuroraAudioProcessorEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseMove(const juce::MouseEvent&) override;
    void mouseExit(const juce::MouseEvent&) override;
    juce::String getTooltip() override;
    bool keyPressed(const juce::KeyPress&) override;
private:
    friend struct EditorPerformanceProbe;
    AuroraAudioProcessor& processor;
    SoftClubLookAndFeel theme;
    juce::ComboBox mode, palette, layout, themeChoice, waveWindowChoice, waveColourChoice, waveStyleChoice, waveZoomChoice, factoryPresetChoice;
    VisualSurface visualSurface;
    juce::TextButton freeze{"Freeze"}, reset{"Reset"}, customise{"Customise"};
    juce::TextButton factoryPresetPrevious{"Previous"}, factoryPresetNext{"Next"};
    int lastFactoryPreset = -1;
    double lastPresetSync = 0;
    juce::String lastPresetAppearance;
    void applyFactoryPreset(int);
    void syncFactoryPreset();
    std::unique_ptr<juce::Component> customizationPanel;
    juce::Viewport customizationDropdown;
    bool customizationOpen = false;
    std::array<int,8> observedChoices {{-1,-1,-1,-1,-1,-1,-1,-1}};
    double observedWaveSeconds = -1;
    int appliedLayout = -1;
    std::array<float,13> spectrogramAppearance {};
    bool spectrogramAppearanceValid = false;
    std::array<std::uint32_t,9> interfaceAppearance {};
    bool interfaceAppearanceValid = false;
    void syncControls();
    void applyLayout();
    void setCustomizationOpen(bool);
    juce::Component sceneClip;
    juce::ScrollBar dashboardScroll{true};
    double dashboardOffset=0;
    std::array<float,Customization::descriptors.size()> cachedCustom{};
    void customizationChanged();
    void showCustomization();
    void scrollBarMoved(juce::ScrollBar*,double position) override;
    std::array<juce::Rectangle<float>,6> moduleAreas() const;
    std::array<std::uint32_t,3> moduleColours(const char*) const;
    double waveformSeconds() const;
    bool usesScope() const;
    std::array<juce::TextButton,3> colourSwatches;
    juce::Slider intensity;
    juce::TooltipWindow tips{this, 600};
    AnalysisFrame frame;
    struct SpectrumTrace { std::vector<float> bins; double time = 0; };
    struct StereoTrace { std::vector<juce::Point<float>> points; double time = 0; };
    std::deque<std::vector<float>> history;
    std::deque<SpectrumTrace> spectrumTrails;
    std::deque<StereoTrace> stereoTrails;
    static constexpr int historyColumns = 480, historyRows = 256;
    juce::Image spectrogramCache, stereoCache;
    std::array<juce::Colour,256> spectralLut{};
    static constexpr int stereoResolution = 256;
    std::vector<float> stereoDensity;
    std::array<std::vector<float>,2> triggeredWave;
    std::deque<WaveformColumn> continuousWave;
    WaveformClock waveformClock;
    std::vector<WaveformProjectionGroup> projectedWave;
    WaveformRibbon waveformRibbon, waveformRetinaRibbon;
    int waveBucketSamples=48;
    bool waveHasTail=false;
    std::uint64_t waveCursor=0,waveGeneration=0,waveTotalSamples=0;
    double waveSampleRate=48000,waveHead=0,lastSceneTime=0;
    float waveRange = 1.0f, waveEnvelope = 0.125f;
    bool waveTriggered = false;
    juce::Rectangle<float> spectrumBounds, spectrogramBounds, waveformBounds, stereoBounds, levelsBounds;
    juce::Point<float> hoverPosition {-1,-1};
    std::array<std::uint32_t,3> cachedColours{};
    int cachedTheme = -1;
    std::uint64_t lastSequence = std::numeric_limits<std::uint64_t>::max();
    double lastHistoryTime = 0, meterTime = 0, lastTimerTime = 0, waveTime = 0;
    float stereoEnvelope = 0.075f;
    float heldL = -120, heldR = -120;
    juce::Colour ink, accent, secondary, paper, line;
    bool isDark() const { const int t=processor.themeMode.load(); return t==1 || t==3; }
    bool isFlightDeck() const { return processor.themeMode.load()==3; }
    void timerCallback() override;
    void clearVisualHistory();
    void updateVisualHistory(const AnalysisFrame&);
    void rebuildSpectrogram();
    void rebuildStereo();
    void updateWaveform(const AnalysisFrame&);
    void updateContinuousWave(double now);
    juce::Rectangle<float> waveformArea() const;
    void positionWaveControls();
    void writeSpectrogramColumn(int column, const std::vector<float>&);
    void setPalette();
    std::array<std::uint32_t,3> currentColours() const;
    void editColour(int index);
    void panel(juce::Graphics&, juce::Rectangle<float>, const juce::String&);
    void text(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float, juce::Colour, bool = false, int = juce::Justification::centredLeft);
    void digits(juce::Graphics&, const juce::String&, juce::Rectangle<float>, float, juce::Colour, int = juce::Justification::centredLeft);
    void spectrum(juce::Graphics&, juce::Rectangle<float>);
    void spectrogram(juce::Graphics&, juce::Rectangle<float>);
    void waveform(juce::Graphics&, juce::Rectangle<float>);
    void scope(juce::Graphics&, juce::Rectangle<float>);
    void stereo(juce::Graphics&, juce::Rectangle<float>);
    void levels(juce::Graphics&, juce::Rectangle<float>);
    void field(juce::Graphics&, juce::Rectangle<float>);
    float binAt(float normalizedX, const std::vector<float>&, float span = 0.005f) const;
    static juce::String number(float value, int decimals = 1);
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AuroraAudioProcessorEditor)
};
