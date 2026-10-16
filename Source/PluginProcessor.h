#pragma once

#include <JuceHeader.h>
#include "Analysis.h"
#include "Palette.h"
#include "Customization.h"
#include <array>
#include <atomic>
#include <cstdint>

/** Transparent audio metering: the host's samples are never modified.
    Only the analysis worker calls AudioAnalyzer. Editor snapshots may be read
    independently through the analyzer's snapshot interface.
*/
class AuroraAudioProcessor final : public juce::AudioProcessor,
                                   private juce::Thread
{
public:
    AuroraAudioProcessor();
    ~AuroraAudioProcessor() override;

    const juce::String getName() const override { return "Aurora"; }
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    void resetMeters() noexcept;
    juce::String createAppearancePreset() const;
    bool applyAppearancePreset (const juce::String& xmlText);
    void resetAppearance();

    AudioAnalyzer analyzer;
    Customization custom;
    std::atomic<int> visualMode { 2 };
    std::atomic<int> palette { auroraDefaultPalette };
    std::atomic<std::uint32_t> customColourA { 0x214e68u };
    std::atomic<std::uint32_t> customColourB { 0x82bac7u };
    std::atomic<std::uint32_t> customColourC { 0xe4f2e9u };
    std::atomic<int> themeMode { auroraDefaultTheme };
    std::atomic<int> layoutMode { 0 };
    std::atomic<int> waveformWindow { 1 }; // Stable IDs: 6 s, 12 s, 24 s, scope, 3 s, 1 s.
    std::atomic<int> waveformColour { 0 }; // Coarse bands or user palette.
    std::atomic<int> waveformStyle { 0 }; // Silk, Prism, Trace: presentation only.
    std::atomic<int> waveformZoom { 0 }; // 1x, 2x, 4x vertical display scale; audio unchanged.
    std::atomic<float> visualGain { 1.0f };
    std::atomic<bool> frozen { false };
    // Session-only view state: appearance presets leave expansion unchanged.
    std::atomic<bool> sceneExpanded { false };
    std::atomic<float> cameraYaw { 0.0f }, cameraPitch { 0.0f }, cameraZoom { 1.0f };

    // Diagnostic values concern analysis only; the audio path is unaffected.
    std::atomic<std::uint64_t> droppedSamples { 0 };
    std::atomic<bool> isReceiving { false };

private:
    void run() override;
    void stopWorker();
    void writeAppearanceState (juce::ValueTree&) const;
    void readAppearanceState (const juce::ValueTree&);

    static constexpr int fifoCapacity = 32768;
    static constexpr int analysisChunkSize = 1024;
    juce::AbstractFifo fifo { fifoCapacity };
    std::array<float, fifoCapacity> fifoLeft {};
    std::array<float, fifoCapacity> fifoRight {};
    std::atomic<bool> acceptSamples { false };
    std::atomic<bool> resetRequested { false };
    std::atomic<bool> discontinuity { false };
    double analysisSampleRate = 48000.0; // Changed only while worker is stopped.
    int analysisChannelCount = 2;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AuroraAudioProcessor)
};
