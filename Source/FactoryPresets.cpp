#include "FactoryPresets.h"
#include "Customization.h"
#include "Palette.h"
#include <vector>

namespace {
using Colours = std::array<std::uint32_t, 3>;
using Surface = std::array<std::uint32_t, 4>; // background, panel, text, grid

struct Look {
    const char* name;
    Colours colours;
    int layout;
    juce::XmlElement state { "AuroraAppearancePreset" };
    juce::String xml;

    Look(const char* title, Colours gradient, Surface surface, int theme, int scene, int arrangement)
        : name(title), colours(gradient), layout(arrangement)
    {
        state.setAttribute("version", 1);
        state.setAttribute("visualMode", scene);
        state.setAttribute("cameraYaw", 0.0);
        state.setAttribute("cameraPitch", 0.0);
        state.setAttribute("cameraZoom", 1.0);
        state.setAttribute("palette", auroraCustomPalette);
        state.setAttribute("customColourA", int(gradient[0]));
        state.setAttribute("customColourB", int(gradient[1]));
        state.setAttribute("customColourC", int(gradient[2]));
        state.setAttribute("themeMode", theme);
        state.setAttribute("layoutMode", arrangement);
        state.setAttribute("waveformWindow", 1);
        state.setAttribute("waveformColour", 1);
        state.setAttribute("waveformStyle", 0);
        state.setAttribute("waveformZoom", 0);
        state.setAttribute("visualGain", 1.0);
        for (const auto& d : Customization::descriptors)
            setting(d.id, d.defaultValue);
        setting("interfaceOverride", 1);
        setting("backgroundColour", float(surface[0]));
        setting("panelColour", float(surface[1]));
        setting("textColour", float(surface[2]));
        setting("gridColour", float(surface[3]));
        // Disabled module overrides still start with this look's own colours,
        // so enabling an override in Customise never causes a surprise jump.
        for (const char* module : {"wave", "spectrum", "spectrogram", "stereo", "levels", "scene"})
            modulePalette(module, gradient, false);
    }

    Look& setting(const char* key, float value)
    {
        const auto* descriptor = Customization::find(key);
        jassert(descriptor != nullptr);
        if (descriptor != nullptr)
            state.setAttribute(juce::String("custom_") + key,
                               double(Customization::sanitise(*descriptor, value)));
        return *this;
    }

    Look& field(const char* key, double value)
    {
        state.setAttribute(key, value);
        return *this;
    }

    Look& modulePalette(const char* prefix, Colours values, bool enabled = true)
    {
        const juce::String base(prefix);
        setting((base + "OwnPalette").toRawUTF8(), enabled ? 1.f : 0.f);
        setting((base + "ColourA").toRawUTF8(), float(values[0]));
        setting((base + "ColourB").toRawUTF8(), float(values[1]));
        setting((base + "ColourC").toRawUTF8(), float(values[2]));
        return *this;
    }

    Look& bands(Colours values)
    {
        field("waveformColour", 0);
        setting("waveLowColour", float(values[0]));
        setting("waveMidColour", float(values[1]));
        setting("waveHighColour", float(values[2]));
        return *this;
    }

    Look& camera(double yaw, double pitch, double zoom)
    {
        return field("cameraYaw", yaw).field("cameraPitch", pitch).field("cameraZoom", zoom);
    }

    Look& finish(float speed, float bloom, float exposure, float saturation)
    {
        return setting("sceneSpeed", speed).setting("sceneBloom", bloom)
            .setting("sceneExposure", exposure).setting("sceneSaturation", saturation);
    }

    Look& history(float seconds)
    {
        return setting("waveCustomTime", 1).setting("waveSeconds", seconds);
    }

    // Module indices: scene, spectrum, spectrogram, waveform, stereo, levels.
    // Supply every module once; visibility is independent of its saved rank.
    Look& grid(int columns, std::array<int, 6> order, unsigned visible = 0x3f)
    {
        static constexpr std::array<const char*, 6> names {
            "Scene", "Spectrum", "Spectrogram", "Waveform", "Stereo", "Levels"
        };
        setting("layoutColumns", float(columns));
        for (int position = 0; position < 6; ++position)
            setting((juce::String("order") + names[size_t(order[size_t(position)])]).toRawUTF8(), float(position + 1));
        for (size_t index = 0; index < names.size(); ++index)
            setting((juce::String("show") + names[index]).toRawUTF8(), (visible & (1u << index)) != 0 ? 1.f : 0.f);
        return *this;
    }

    void seal() { xml = state.toString(juce::XmlElement::TextFormat().withoutHeader()); }
};

const std::vector<Look>& collection()
{
    // These are hand-composed combinations of material, camera, timing and
    // spatial hierarchy, not automatic hue rotations. Measurement thresholds
    // and input gain retain their normal defaults throughout the collection.
    static const auto looks = [] {
        std::vector<Look> result;
        result.reserve(24);
        const auto add = [&result](const char* name, Colours colours, Surface surface,
                                   int theme, int scene, int layout) -> Look& {
            result.emplace_back(name, colours, surface, theme, scene, layout);
            return result.back();
        };

        add("Pearl Cloud", {0x555b91, 0xb9ddd8, 0xffe9dc}, {0x151923, 0x10151e, 0xe8e7ed, 0x353c50}, 1, 2, 0)
            .finish(.75f, 1.25f, 1.05f, .85f).camera(-.16, .12, 1.02)
            .setting("layoutSceneWidth", .65f).setting("layoutTopHeight", .65f)
            .setting("waveDensity", 1.25f).setting("waveGradient", 1.35f).setting("waveCrest", 1.15f)
            .setting("spectrumTrail", .32f).setting("stereoTrail", .58f)
            .modulePalette("spectrogram", {0x20274d, 0x96cbd3, 0xffedcf});

        add("Black Opal", {0x2e215e, 0xda82c4, 0xaef4d4}, {0x0d0e17, 0x080b13, 0xece5f4, 0x352b45}, 1, 2, 3)
            .grid(2, {0, 3, 1, 5, 2, 4}, 0x2b).finish(.65f, 1.55f, 1.1f, 1.2f).camera(.3, -.17, 1.08)
            .history(8).setting("layoutGap", 12).setting("waveGradient", 1.65f).setting("waveCore", .75f)
            .setting("spectrumFill", .75f).setting("spectrumTrail", .48f)
            .modulePalette("wave", {0x532578, 0xbc9cf3, 0xe2f9e3});

        add("Sea Glass", {0x125368, 0x70decb, 0xf0eac4}, {0x142526, 0x0b1a20, 0xe0efdf, 0x36534f}, 3, 1, 0)
            .finish(.8f, .85f, 1.1f, .85f).camera(-.42, .18, .92)
            .setting("layoutSceneWidth", .56f).setting("layoutTopHeight", .58f).setting("layoutGap", 10)
            .bands({0xe5bb78, 0x72dec9, 0x9ecbf4}).setting("waveBands", 1.25f)
            .setting("stereoTrail", .65f).setting("spectrogramContrast", .9f)
            .modulePalette("scene", {0x164d60, 0x81d5c1, 0xffffdc});

        add("Rose Quartz", {0x64314f, 0xe6a8b9, 0xffe4cc}, {0x241a24, 0x1b121d, 0xf4e3e8, 0x583948}, 1, 0, 3)
            .grid(2, {0, 2, 3, 1, 4, 5}).finish(.65f, 1.25f, 1.05f, .85f).camera(.5, .26, 1.08)
            .setting("layoutGap", 14).history(16).setting("waveCore", 1.25f).setting("waveGradient", 1.5f)
            .setting("spectrumTrail", .4f).setting("spectrogramSeconds", 16)
            .modulePalette("stereo", {0x6b345f, 0xbfa6ea, 0xffe4cc});

        add("Amber Archive", {0x6a3824, 0xf1b05c, 0xffebad}, {0x211e18, 0x13150f, 0xeee6ca, 0x514c32}, 3, 3, 1)
            .bands({0xf0a864, 0xe6d380, 0xa6dcc4}).field("waveformStyle", 1).history(18)
            .setting("waveDensity", 1.35f).setting("waveCore", .7f).setting("spectrumTrail", .6f)
            .setting("spectrogramSeconds", 18).setting("spectrogramContrast", 1.2f)
            .setting("levelsFloor", -72).setting("stereoTrail", .78f).setting("layoutGap", 6)
            .finish(.65f, .75f, 1.f, .85f);

        add("Cobalt Night", {0x202e7a, 0x559eef, 0xc5f4f5}, {0x101725, 0x0a101d, 0xe0eafa, 0x2e405b}, 3, 3, 0)
            .finish(1.1f, 1.4f, 1.1f, 1.2f).camera(.24, -.14, 1.02)
            .setting("layoutSceneWidth", .67f).setting("layoutTopHeight", .64f)
            .setting("waveGradient", 1.55f).setting("waveCrest", 1.35f).history(6)
            .setting("spectrumTrail", .18f).setting("stereoTrail", .32f)
            .modulePalette("levels", {0x183854, 0x6acede, 0xd6efff});

        add("Ultraviolet", {0x39256f, 0xb08ef4, 0xe7ccfa}, {0x1a1329, 0x100e1d, 0xeee5fa, 0x45355b}, 1, 5, 3)
            .grid(3, {0, 1, 4, 3, 2, 5}).finish(.85f, 1.35f, 1.05f, 1.2f).camera(-.56, .28, 1.05)
            .setting("layoutGap", 12).history(9).field("waveformStyle", 1)
            .setting("waveDensity", 1.35f).setting("waveCrest", 1.25f).setting("stereoTrail", .85f)
            .modulePalette("spectrogram", {0x28144d, 0x9b83df, 0xecdbf7});

        add("Afterglow", {0x682c48, 0xf18a6d, 0xffdba0}, {0x241717, 0x180f13, 0xf7e6d3, 0x57372f}, 1, 0, 0)
            .finish(.8f, 1.6f, 1.05f, 1.1f).camera(-.35, -.16, .96)
            .setting("layoutTopHeight", .56f).setting("layoutSceneWidth", .62f).setting("layoutGap", 12)
            .bands({0xffad76, 0xe887a8, 0xc2acf2}).setting("waveGradient", 1.35f)
            .setting("spectrogramContrast", 1.1f).setting("stereoTrail", .55f)
            .modulePalette("spectrum", {0x6e3551, 0xf29e82, 0xffe3b2});

        add("Glacier", {0x244875, 0x8bcfe1, 0xeaf7fd}, {0x15212b, 0x0c1821, 0xe2f1f5, 0x354a59}, 3, 4, 0)
            .finish(.6f, .8f, 1.15f, .7f).camera(.26, .15, 1.04)
            .setting("layoutSceneWidth", .55f).setting("layoutTopHeight", .65f)
            .setting("waveDensity", 1.4f).setting("waveCrest", 1.4f).setting("waveCore", .7f)
            .setting("spectrumLine", 1.1f).setting("spectrumTrail", .45f).setting("stereoTrail", .7f)
            .modulePalette("spectrogram", {0x17344b, 0x6fa9c8, 0xe6f8ee});

        add("Verdigris", {0x214e49, 0x77bc9c, 0xe5d99b}, {0x1a2420, 0x101a18, 0xe6e9cd, 0x3e5142}, 3, 1, 3)
            .grid(2, {2, 3, 4, 5, 0, 1}, 0x3c).history(20).field("waveformStyle", 1)
            .bands({0xdab46c, 0x79c8b0, 0xa1c9cc}).setting("waveBands", 1.35f)
            .setting("spectrogramSeconds", 20).setting("spectrogramContrast", 1.15f)
            .setting("stereoTrail", .9f).setting("layoutGap", 10).setting("levelsFloor", -72)
            .finish(.55f, .8f, 1.f, .75f);

        add("Supernova", {0x452254, 0xfb796e, 0xffe8a6}, {0x171020, 0x0d0a17, 0xf7e8dd, 0x4e344d}, 1, 4, 3)
            .grid(1, {0, 3, 1, 2, 4, 5}, 0x09).finish(1.15f, 1.7f, 1.1f, 1.3f).camera(.42, -.3, .94)
            .history(8).setting("layoutGap", 16).setting("waveGradient", 1.8f)
            .setting("waveCrest", 1.4f).setting("waveDensity", 1.35f).setting("waveGrid", 0)
            .modulePalette("wave", {0x703764, 0xe49dc4, 0xffe2b6});

        add("Velvet Static", {0x48305f, 0xbca2cf, 0xf3d7ac}, {0x211a26, 0x14101b, 0xece3ef, 0x4a3a51}, 1, 2, 2)
            .history(16).setting("waveGradient", 1.7f).setting("waveCore", 1.2f).setting("waveDensity", 1.35f)
            .setting("waveCrest", 1.3f).setting("layoutLevelWidth", .15f).setting("layoutGap", 14)
            .setting("levelsSegments", 0).setting("paletteMiddle", .58f)
            .modulePalette("levels", {0x4a344a, 0xc9b5df, 0xf4d6b3}).finish(.5f, 1.2f, 1.f, .75f);

        add("Citrus Circuit", {0x315647, 0xb3df69, 0xe8efb0}, {0x1c241d, 0x111b18, 0xeaf0d2, 0x48583b}, 3, 5, 1)
            .bands({0xe9be70, 0xbce974, 0x87dfd4}).history(6).field("waveformStyle", 1)
            .setting("waveBands", 1.3f).setting("waveCrest", 1.35f).setting("spectrumTrail", .2f)
            .setting("spectrogramSeconds", 8).setting("stereoTrail", .35f).setting("layoutGap", 8)
            .modulePalette("spectrogram", {0x143f49, 0x62baa9, 0xe9eea8}).finish(1.15f, 1.f, 1.f, 1.15f);

        add("Moonstone", {0x515d89, 0x78aaa6, 0xd8b2aa}, {0xe8ebe9, 0xf2f2ee, 0x273d48, 0xc2ccce}, 0, 2, 0)
            .finish(.55f, .75f, 1.f, .6f).camera(-.2, .16, 1.08)
            .setting("layoutGap", 14).setting("layoutSceneWidth", .63f).setting("layoutTopHeight", .62f)
            .setting("waveCore", .85f).setting("waveCrest", 1.3f).setting("spectrumFill", .6f)
            .setting("spectrumTrail", .28f).setting("stereoBrightness", .85f)
            .modulePalette("scene", {0x647093, 0xc1dbd2, 0xf6dfd1});

        add("Porcelain", {0x314d70, 0x759caa, 0xb7998b}, {0xe5e7ea, 0xf2f3f3, 0x283746, 0xbcc5ce}, 2, 4, 2)
            .history(10).setting("waveDensity", 1.4f).setting("waveCore", .8f).setting("waveCrest", 1.45f)
            .setting("waveGradient", .75f).setting("layoutGap", 16).setting("layoutLevelWidth", .14f)
            .setting("levelsSegments", 0).setting("paletteMiddle", .6f)
            .modulePalette("levels", {0x3e5368, 0x668da1, 0xa78072}).finish(.5f, .55f, 1.f, .55f);

        add("Infrared", {0x592539, 0xf47569, 0xffc898}, {0x25191b, 0x170d12, 0xf5e4de, 0x58353d}, 3, 3, 3)
            .grid(3, {3, 1, 2, 4, 5, 0}, 0x3e).history(6).field("waveformStyle", 1)
            .setting("waveDensity", 1.45f).setting("waveCrest", 1.35f).setting("waveCore", .65f)
            .setting("spectrogramContrast", 1.25f).setting("spectrogramSeconds", 9).setting("spectrumTrail", .12f)
            .setting("layoutGap", 6).setting("stereoTrail", .3f)
            .modulePalette("stereo", {0x66354a, 0xd68da1, 0xffc4a4}).finish(1.3f, 1.1f, 1.f, 1.2f);

        add("Abyss", {0x16355c, 0x338fa6, 0x8de5d4}, {0x0c141c, 0x070e17, 0xd5ecee, 0x263d4e}, 1, 5, 3)
            .grid(2, {0, 3, 1, 2, 4, 5}, 0x09).finish(.4f, 1.45f, .95f, 1.05f).camera(-.62, -.2, 1.1)
            .history(24).setting("layoutGap", 18).setting("waveGradient", 1.8f).setting("waveCore", .75f)
            .setting("waveDensity", 1.45f).setting("waveGrid", 0).setting("waveLabels", 0)
            .modulePalette("wave", {0x234d6f, 0x67b9c4, 0xb4f5d8});

        add("Honeycomb", {0x675036, 0xdcc17a, 0xf8edb6}, {0x27251d, 0x191a13, 0xf0ead2, 0x56553a}, 3, 1, 0)
            .finish(.7f, .95f, 1.1f, .8f).camera(.43, -.24, .98)
            .setting("layoutSceneWidth", .52f).setting("layoutTopHeight", .59f).setting("layoutGap", 10)
            .bands({0xe8b679, 0xcdce80, 0x98c8bc}).setting("waveCore", 1.3f).setting("waveBands", 1.15f)
            .setting("spectrumTrail", .45f).setting("stereoTrail", .6f)
            .modulePalette("spectrogram", {0x374839, 0xabb379, 0xf7e8b2});

        add("Sakura", {0x685077, 0xc390a5, 0xc8b291}, {0xeee5e5, 0xf8efed, 0x513b4b, 0xd6bdc8}, 0, 0, 3)
            .grid(2, {0, 3, 1, 5, 2, 4}, 0x2b).finish(.65f, .7f, 1.05f, .7f).camera(.24, .32, 1.02)
            .history(12).setting("layoutGap", 16).setting("waveGradient", 1.3f).setting("waveCrest", 1.25f)
            .setting("spectrumFill", .55f).setting("spectrumTrail", .35f).setting("levelsSegments", 0)
            .modulePalette("scene", {0x755d94, 0xe7b8c7, 0xf3e4c7});

        add("Magnetic Tape", {0x405449, 0xb3bf87, 0xe1b383}, {0x222820, 0x121c17, 0xe6e7d0, 0x4b5740}, 3, 3, 1)
            .bands({0xd9a77c, 0xb8cd8c, 0x8dc7be}).history(24).field("waveformStyle", 2)
            .setting("waveFill", .8f).setting("waveDensity", 1.15f).setting("waveCrest", 1.45f)
            .setting("spectrogramSeconds", 24).setting("spectrogramContrast", .8f).setting("spectrumTrail", .7f)
            .setting("stereoTrail", 1.1f).setting("levelsFloor", -72).setting("layoutGap", 6)
            .finish(.6f, .5f, 1.f, .65f);

        add("Blue Hour", {0x283f6f, 0x829dce, 0xe7bd92}, {0x171e30, 0x101627, 0xe4e7f4, 0x37445f}, 1, 2, 2)
            .history(8).setting("waveGradient", 1.65f).setting("waveDensity", 1.45f).setting("waveCrest", 1.3f)
            .setting("waveCore", .85f).setting("layoutLevelWidth", .21f).setting("layoutGap", 12)
            .modulePalette("levels", {0x304269, 0x9cafd3, 0xecc79e}).finish(.7f, 1.25f, 1.f, .9f);

        add("Mercury", {0x465460, 0xa8bcc0, 0xe3e5cf}, {0x1d2429, 0x11191f, 0xe7ece7, 0x45545a}, 3, 2, 0)
            .finish(.45f, 1.1f, 1.1f, .25f).camera(.18, -.22, 1.12)
            .setting("layoutSceneWidth", .7f).setting("layoutTopHeight", .65f).setting("layoutGap", 12)
            .setting("waveGradient", 1.45f).setting("waveCore", .65f).setting("waveCrest", 1.5f)
            .setting("spectrumFill", .55f).setting("spectrumLine", 1.1f).setting("stereoTrail", .7f)
            .setting("levelsSegments", 0);

        add("Laser Bloom", {0x353c76, 0xe588d9, 0x97ecdd}, {0x1b1730, 0x100e20, 0xf1e7f5, 0x4a3a63}, 1, 4, 3)
            .grid(3, {3, 1, 0, 2, 4, 5}).finish(1.25f, 1.65f, 1.05f, 1.3f).camera(-.3, .25, 1.02)
            .bands({0xf2a177, 0x83e0cb, 0xc99aed}).history(6).field("waveformStyle", 1)
            .setting("waveBands", 1.4f).setting("waveCrest", 1.4f).setting("waveDensity", 1.3f)
            .setting("spectrogramContrast", 1.2f).setting("spectrumTrail", .2f).setting("stereoTrail", .48f)
            .modulePalette("spectrogram", {0x28205b, 0xb775d4, 0xaff8dc});

        add("Quiet Geometry", {0x375950, 0x89b8a3, 0xd8d7b0}, {0x202822, 0x141e1a, 0xe4e8d8, 0x435448}, 3, 0, 3)
            .grid(2, {3, 1, 4, 5, 0, 2}, 0x3a).history(12).field("waveformStyle", 2)
            .setting("waveFill", .6f).setting("waveDensity", .8f).setting("waveCrest", 1.4f)
            .setting("waveCore", .5f).setting("layoutGap", 18).setting("spectrumFill", .3f)
            .setting("spectrumTrail", .08f).setting("spectrumLine", 1.1f).setting("stereoTrail", .3f)
            .setting("levelsSegments", 0).finish(.5f, .45f, 1.f, .65f);

        for (auto& look : result) look.seal();
        return result;
    }();
    return looks;
}

const Look* at(int index) noexcept
{
    const auto& looks = collection();
    return index >= 0 && size_t(index) < looks.size() ? &looks[size_t(index)] : nullptr;
}

bool integerField(const juce::String& name)
{
    if (name.startsWith("custom_")) {
        const auto* d = Customization::find(name.substring(7).toRawUTF8());
        return d != nullptr && d->kind != Customization::Kind::Slider;
    }
    return name != "visualGain" && name != "cameraYaw" && name != "cameraPitch" && name != "cameraZoom";
}

bool numericValue(const juce::String& source, double& value)
{
    const auto text = source.trim();
    if (text.isEmpty() || text.length() > 64) return false;
    auto* c = text.toRawUTF8();
    if (*c == '+' || *c == '-') ++c;
    bool digits = false;
    while (*c >= '0' && *c <= '9') { digits = true; ++c; }
    if (*c == '.') {
        ++c;
        while (*c >= '0' && *c <= '9') { digits = true; ++c; }
    }
    if (!digits) return false;
    if (*c == 'e' || *c == 'E') {
        ++c;
        if (*c == '+' || *c == '-') ++c;
        bool exponent = false;
        while (*c >= '0' && *c <= '9') { exponent = true; ++c; }
        if (!exponent) return false;
    }
    if (*c != 0) return false;
    value = text.getDoubleValue();
    return std::isfinite(value);
}
} // namespace

int factoryPresetCount() noexcept { return int(collection().size()); }
const char* factoryPresetName(int index) noexcept { const auto* look = at(index); return look ? look->name : ""; }
Colours factoryPresetColours(int index) noexcept { const auto* look = at(index); return look ? look->colours : Colours{}; }
int factoryPresetLayout(int index) noexcept { const auto* look = at(index); return look ? look->layout : -1; }
juce::String factoryPresetXml(int index) { const auto* look = at(index); return look ? look->xml : juce::String{}; }

int findFactoryPreset(const juce::String& appearanceXml)
{
    if (appearanceXml.getNumBytesAsUTF8() > 256 * 1024 || appearanceXml.contains("<!")) return -1;
    auto body = appearanceXml.trim();
    if (body.startsWith("<?xml ")) {
        const auto end = body.indexOf("?>");
        if (end < 0) return -1;
        body = body.substring(end + 2).trim();
    }
    if (body.contains("<?") || body.contains("</AuroraPresetEnvelope")) return -1;
    const auto envelope = juce::XmlDocument::parse("<AuroraPresetEnvelope>" + body + "</AuroraPresetEnvelope>");
    if (!envelope || envelope->getNumChildElements() != 1) return -1;
    const auto* input = envelope->getFirstChildElement();
    if (!input || !input->hasTagName("AuroraAppearancePreset") || input->getNumChildElements() != 0
        || input->getStringAttribute("version") != "1") return -1;
    const auto& looks = collection();
    for (size_t index = 0; index < looks.size(); ++index) {
        const auto& expected = looks[index].state;
        bool matches = true;
        for (int attribute = 0; attribute < expected.getNumAttributes(); ++attribute) {
            const auto key = expected.getAttributeName(attribute);
            double actual = 0;
            if (!input->hasAttribute(key) || !numericValue(input->getStringAttribute(key), actual)) {
                matches = false;
                break;
            }
            const double wanted = expected.getDoubleAttribute(key);
            if (integerField(key) ? actual != wanted : std::abs(actual - wanted) > 1.e-5) {
                matches = false;
                break;
            }
        }
        if (matches) return int(index);
    }
    return -1;
}
