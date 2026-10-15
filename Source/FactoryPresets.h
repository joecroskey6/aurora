#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cstdint>

// Original, immutable whole-appearance presets. Applying the returned XML uses
// the same validated path as an imported user preset and leaves audio alone.
int factoryPresetCount() noexcept;
const char* factoryPresetName(int index) noexcept;
std::array<std::uint32_t, 3> factoryPresetColours(int index) noexcept;
int factoryPresetLayout(int index) noexcept; // Studio / Meters / Waveform / Custom
juce::String factoryPresetXml(int index);

// Recognises an unedited look after host restore or user-preset import. Numeric
// XML formatting is immaterial; every appearance field must match. Returns -1
// for an edited, incomplete or invalid appearance. No processor is constructed.
int findFactoryPreset(const juce::String& appearanceXml);
