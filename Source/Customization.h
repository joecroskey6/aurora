#pragma once

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>

// Original presentation-only settings. Reads and writes are allocation-free;
// colours use 24-bit RGB integers, all exactly representable by float.
class Customization final {
public:
    enum class Section { Colours, Waveform, Spectrum, Spectrogram, Stereo, Levels, Scene, Layout };
    enum class Kind { Slider, Toggle, Colour, Choice };
    struct Descriptor {
        const char* id;
        const char* label;
        Section section;
        Kind kind;
        float defaultValue, min, max, step;
        const char* choices; // Pipe-separated names for Choice; empty otherwise.
    };
    inline static constexpr std::array descriptors {
        Descriptor {"interfaceOverride", "Custom interface colours", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"backgroundColour", "Background", Section::Colours, Kind::Colour, 1514266.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"panelColour", "Panels", Section::Colours, Kind::Colour, 988949.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"textColour", "Text", Section::Colours, Kind::Colour, 14868431.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"gridColour", "Grid", Section::Colours, Kind::Colour, 3490107.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"clipColour", "Clip marker", Section::Colours, Kind::Colour, 16742751.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"paletteMiddle", "Gradient midpoint (meters)", Section::Colours, Kind::Slider, 0.5f, 0.1f, 0.9f, 0.01f, ""},
        Descriptor {"paletteReverse", "Reverse gradient", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"waveOwnPalette", "Waveform custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"waveColourA", "Waveform start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveColourB", "Waveform middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveColourC", "Waveform end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrumOwnPalette", "Spectrum custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"spectrumColourA", "Spectrum start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrumColourB", "Spectrum middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrumColourC", "Spectrum end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrogramOwnPalette", "Spectrogram custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"spectrogramColourA", "Spectrogram start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrogramColourB", "Spectrogram middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"spectrogramColourC", "Spectrogram end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"stereoOwnPalette", "Stereo custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"stereoColourA", "Stereo start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"stereoColourB", "Stereo middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"stereoColourC", "Stereo end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"levelsOwnPalette", "Levels custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"levelsColourA", "Levels start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"levelsColourB", "Levels middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"levelsColourC", "Levels end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"sceneOwnPalette", "Scene custom palette", Section::Colours, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"sceneColourA", "Scene start", Section::Colours, Kind::Colour, 1195829.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"sceneColourB", "Scene middle", Section::Colours, Kind::Colour, 6610860.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"sceneColourC", "Scene end", Section::Colours, Kind::Colour, 15266212.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveLowColour", "Low frequencies", Section::Waveform, Kind::Colour, 16758651.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveMidColour", "Mid frequencies", Section::Waveform, Kind::Colour, 6482896.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveHighColour", "High frequencies", Section::Waveform, Kind::Colour, 11048959.0f, 0.0f, 16777215.0f, 1.0f, ""},
        Descriptor {"waveDensity", "PCM density", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"waveCore", "RMS body", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"waveCrest", "Peak crest", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"waveBands", "Band detail", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"waveFill", "Fill opacity", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 1.0f, 0.05f, ""},
        Descriptor {"waveGradient", "Gradient depth", Section::Waveform, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"waveGain", "Display gain", Section::Waveform, Kind::Slider, 1.0f, 0.25f, 4.0f, 0.05f, ""},
        Descriptor {"waveCustomTime", "Custom time window", Section::Waveform, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"waveSeconds", "Seconds", Section::Waveform, Kind::Slider, 12.0f, 0.25f, 24.0f, 0.25f, ""},
        Descriptor {"waveGrid", "Grid", Section::Waveform, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"waveLabels", "Labels", Section::Waveform, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"waveChannels", "Channels", Section::Waveform, Kind::Choice, 0.0f, 0.0f, 3.0f, 1.0f, "Stereo|Left|Right|Overlay"},
        Descriptor {"spectrumFloor", "Floor (dBFS)", Section::Spectrum, Kind::Slider, -90.0f, -120.0f, -30.0f, 1.0f, ""},
        Descriptor {"spectrumCeiling", "Ceiling (dBFS)", Section::Spectrum, Kind::Slider, 0.0f, -12.0f, 12.0f, 1.0f, ""},
        Descriptor {"spectrumMinHz", "Minimum frequency (Hz)", Section::Spectrum, Kind::Slider, 20.0f, 10.0f, 500.0f, 1.0f, ""},
        Descriptor {"spectrumMaxHz", "Maximum frequency (Hz)", Section::Spectrum, Kind::Slider, 20000.0f, 2000.0f, 24000.0f, 100.0f, ""},
        Descriptor {"spectrumLine", "Line width", Section::Spectrum, Kind::Slider, 1.3f, 0.5f, 4.0f, 0.1f, ""},
        Descriptor {"spectrumFill", "Fill strength", Section::Spectrum, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"spectrumTrail", "History (seconds)", Section::Spectrum, Kind::Slider, 0.22f, 0.0f, 1.5f, 0.01f, ""},
        Descriptor {"spectrumGrid", "Grid", Section::Spectrum, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"spectrogramFloor", "Floor (dBFS)", Section::Spectrogram, Kind::Slider, -90.0f, -120.0f, -30.0f, 1.0f, ""},
        Descriptor {"spectrogramCeiling", "Ceiling (dBFS)", Section::Spectrogram, Kind::Slider, 0.0f, -12.0f, 12.0f, 1.0f, ""},
        Descriptor {"spectrogramSeconds", "History (seconds)", Section::Spectrogram, Kind::Slider, 12.0f, 3.0f, 24.0f, 1.0f, ""},
        Descriptor {"spectrogramContrast", "Contrast", Section::Spectrogram, Kind::Slider, 1.0f, 0.3f, 2.5f, 0.05f, ""},
        Descriptor {"spectrogramGrid", "Grid", Section::Spectrogram, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"spectrogramReverse", "Reverse direction", Section::Spectrogram, Kind::Toggle, 0.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"stereoTrail", "Persistence (seconds)", Section::Stereo, Kind::Slider, 0.42f, 0.05f, 2.0f, 0.01f, ""},
        Descriptor {"stereoGain", "Display gain", Section::Stereo, Kind::Slider, 1.0f, 0.25f, 4.0f, 0.05f, ""},
        Descriptor {"stereoBrightness", "Brightness", Section::Stereo, Kind::Slider, 1.0f, 0.2f, 3.0f, 0.05f, ""},
        Descriptor {"stereoAutoGain", "Automatic display gain", Section::Stereo, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"stereoGrid", "Grid", Section::Stereo, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"stereoRotation", "Rotation (degrees)", Section::Stereo, Kind::Slider, 0.0f, -180.0f, 180.0f, 1.0f, ""},
        Descriptor {"levelsFloor", "Floor (dBFS)", Section::Levels, Kind::Slider, -60.0f, -96.0f, -24.0f, 6.0f, ""},
        Descriptor {"levelsWarning", "Warning threshold (dBFS)", Section::Levels, Kind::Slider, -3.0f, -18.0f, 0.0f, 1.0f, ""},
        Descriptor {"levelsRms", "RMS", Section::Levels, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"levelsHold", "Peak hold", Section::Levels, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"levelsSegments", "Segments", Section::Levels, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"levelsLoudness", "Loudness", Section::Levels, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"sceneSpeed", "Flow speed", Section::Scene, Kind::Slider, 1.0f, 0.0f, 3.0f, 0.05f, ""},
        Descriptor {"sceneSensitivity", "Music sensitivity", Section::Scene, Kind::Slider, 1.0f, 0.0f, 3.0f, 0.05f, ""},
        Descriptor {"sceneBloom", "Bloom", Section::Scene, Kind::Slider, 1.0f, 0.0f, 3.0f, 0.05f, ""},
        Descriptor {"sceneExposure", "Exposure", Section::Scene, Kind::Slider, 1.0f, 0.25f, 2.0f, 0.05f, ""},
        Descriptor {"sceneSaturation", "Saturation", Section::Scene, Kind::Slider, 1.0f, 0.0f, 2.0f, 0.05f, ""},
        Descriptor {"sceneFrameRate", "Frame rate", Section::Scene, Kind::Choice, 1.0f, 0.0f, 2.0f, 1.0f, "15|30|60"},
        Descriptor {"layoutColumns", "Columns", Section::Layout, Kind::Slider, 2.0f, 1.0f, 3.0f, 1.0f, ""},
        Descriptor {"layoutGap", "Panel spacing", Section::Layout, Kind::Slider, 8.0f, 4.0f, 24.0f, 1.0f, ""},
        Descriptor {"layoutSceneWidth", "Scene width", Section::Layout, Kind::Slider, 0.6f, 0.4f, 0.72f, 0.01f, ""},
        Descriptor {"layoutTopHeight", "Upper row height", Section::Layout, Kind::Slider, 0.63f, 0.45f, 0.75f, 0.01f, ""},
        Descriptor {"layoutLevelWidth", "Levels width", Section::Layout, Kind::Slider, 0.17f, 0.12f, 0.25f, 0.01f, ""},
        Descriptor {"orderScene", "Scene position", Section::Layout, Kind::Slider, 1.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"orderSpectrum", "Spectrum position", Section::Layout, Kind::Slider, 2.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"orderSpectrogram", "Spectrogram position", Section::Layout, Kind::Slider, 3.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"orderWaveform", "Waveform position", Section::Layout, Kind::Slider, 4.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"orderStereo", "Stereo position", Section::Layout, Kind::Slider, 5.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"orderLevels", "Levels position", Section::Layout, Kind::Slider, 6.0f, 1.0f, 6.0f, 1.0f, ""},
        Descriptor {"showScene", "Show scene", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"showSpectrum", "Show spectrum", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"showSpectrogram", "Show spectrogram", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"showWaveform", "Show waveform", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"showStereo", "Show stereo", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
        Descriptor {"showLevels", "Show levels", Section::Layout, Kind::Toggle, 1.0f, 0.0f, 1.0f, 1.0f, ""},
    };
    static constexpr std::size_t count = descriptors.size();

    Customization() noexcept { resetAll(); }
    float get(const char* id) const noexcept {
        const auto index = indexOf(id);
        return index < count ? values[index].load(std::memory_order_relaxed) : 0.f;
    }
    bool set(const char* id, float value) noexcept {
        const auto index = indexOf(id);
        if(index == count)return false;
        const auto next = sanitise(descriptors[index], value);
        // Panel order controls form a permutation: assigning a position swaps
        // its former occupant back into this panel's previous position. Valid
        // permutations also restore correctly through sequential host reads.
        if (isOrderKey(descriptors[index].id)) {
            const auto previous = values[index].load(std::memory_order_relaxed);
            for (std::size_t other = 0; other < count; ++other)
                if (other != index && isOrderKey(descriptors[other].id)
                    && values[other].load(std::memory_order_relaxed) == next) {
                    values[other].store(previous, std::memory_order_relaxed);
                    break;
                }
        }
        values[index].store(next, std::memory_order_relaxed);
        return true;
    }
    void resetSection(Section section) noexcept {
        for(std::size_t index=0;index<count;++index)
            if(descriptors[index].section==section)
                values[index].store(descriptors[index].defaultValue,std::memory_order_relaxed);
    }
    void resetAll() noexcept {
        for(std::size_t index=0;index<count;++index)
            values[index].store(descriptors[index].defaultValue,std::memory_order_relaxed);
    }
    static float sanitise(const Descriptor& descriptor,float value) noexcept {
        if(!std::isfinite(value))return descriptor.defaultValue;
        double result=std::clamp(double(value),double(descriptor.min),double(descriptor.max));
        if(descriptor.step>0)
            result=double(descriptor.min)+std::round((result-double(descriptor.min))/double(descriptor.step))*double(descriptor.step);
        return static_cast<float>(std::clamp(result,double(descriptor.min),double(descriptor.max)));
    }
    static const Descriptor* find(const char* id) noexcept {
        const auto index=indexOf(id);
        return index<count?&descriptors[index]:nullptr;
    }

private:
    static bool isOrderKey(const char* id) noexcept {
        return std::strncmp(id, "order", 5) == 0;
    }
    static std::size_t indexOf(const char* id) noexcept {
        if(id!=nullptr)for(std::size_t index=0;index<count;++index)
            if(std::strcmp(id,descriptors[index].id)==0)return index;
        return count;
    }
    std::array<std::atomic<float>, count> values {};
};
