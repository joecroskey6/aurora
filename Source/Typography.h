#pragma once

#include <juce_graphics/juce_graphics.h>

// Use the matching system families on each platform. Naming macOS-only fonts
// on Windows can fall back to a proportional face even for numeric readouts.
namespace auroraTypography {
inline juce::String sansTypeface() {
#if JUCE_MAC
    return "Helvetica Neue";
#elif JUCE_WINDOWS
    return "Segoe UI";
#else
    return juce::Font::getDefaultSansSerifFontName();
#endif
}
inline juce::String monoTypeface() {
#if JUCE_MAC
    return "Menlo";
#elif JUCE_WINDOWS
    return "Consolas";
#else
    return juce::Font::getDefaultMonospacedFontName();
#endif
}
}
