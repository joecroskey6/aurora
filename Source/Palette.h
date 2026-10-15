#pragma once
#include <array>
#include <cstdint>

// Selector IDs are serialized in host sessions. Keep Custom at 6 and append
// new presets so projects saved with the original six palettes are unchanged.
inline constexpr int auroraCustomPalette = 6;
inline constexpr int auroraPaletteCount = 13;
inline constexpr int auroraFlightDeckTheme = 3;
inline constexpr int auroraThemeCount = 4;
inline constexpr int auroraDefaultPalette = 7;
inline constexpr int auroraDefaultTheme = auroraFlightDeckTheme;
inline constexpr std::array<const char*, auroraPaletteCount> auroraPaletteNames {{
    "Glacier", "Lagoon", "Ultraviolet", "Apricot", "Moss", "Pearl", "Custom",
    "Phosphor", "Amberline", "Ion Blue", "Redshift", "Petrol", "Tape Violet"
}};

// Original three-stop palettes. Custom colours use the identical rendering path.
inline std::array<std::uint32_t, 3> auroraPalette(int index,
    std::uint32_t a, std::uint32_t b, std::uint32_t c)
{
    static constexpr std::array<std::array<std::uint32_t, 3>, auroraPaletteCount> presets {{
        {{0x193f59, 0x70bbc4, 0xf0f7eb}},
        {{0x124d56, 0x50b898, 0xe2edb8}},
        {{0x503b82, 0xb77bab, 0xf5d8d3}},
        {{0x723752, 0xdba579, 0xfff0c5}},
        {{0x3e5845, 0x93b484, 0xe4ebc1}},
        {{0x626d94, 0xc9dada, 0xf9f5e8}},
        {{0x000000, 0x000000, 0x000000}}, // Custom uses the supplied RGB below.
        {{0x123f35, 0x64dfac, 0xe8f1a4}},
        {{0x592f35, 0xeca75c, 0xffeac2}},
        {{0x263a73, 0x63bce0, 0xe8ecd9}},
        {{0x482346, 0xde6974, 0xf7dcad}},
        {{0x0d424d, 0x55c5ba, 0xd8eaa3}},
        {{0x373058, 0xa68cdd, 0xf0c6b0}}
    }};
    return index >= 0 && index < auroraPaletteCount && index != auroraCustomPalette
        ? presets[static_cast<std::size_t>(index)]
        : std::array<std::uint32_t, 3>{{a, b, c}};
}
