#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

class AuroraAudioProcessor;

// A responsive, keyboard-accessible gallery. The component owns its viewport;
// selecting a card applies appearance only and preserves the audio analysis.
std::unique_ptr<juce::Component> createFactoryPresetBrowser(
    AuroraAudioProcessor&, std::function<void()> onChange);
