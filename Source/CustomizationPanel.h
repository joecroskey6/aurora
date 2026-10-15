#pragma once

#include <JuceHeader.h>
#include <functional>
#include <memory>

class AuroraAudioProcessor;

// Own this component in an editor-owned window. All edits are presentation-only
// and notify the owner immediately; the panel owns its complete look-and-feel.
std::unique_ptr<juce::Component> createCustomizationPanel(
    AuroraAudioProcessor&, std::function<void()> onChange);
