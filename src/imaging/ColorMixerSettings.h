#pragma once

#include <algorithm>
#include <array>
#include <cstddef>

namespace imaging {

constexpr std::size_t kColorMixerBandCount = 8;

enum class ColorMixerBand : std::size_t {
    Red = 0,
    Orange,
    Yellow,
    Green,
    Aqua,
    Blue,
    Purple,
    Magenta,
};

struct ColorMixerSettings {
    std::array<int, kColorMixerBandCount> hue{};
    std::array<int, kColorMixerBandCount> saturation{};
    std::array<int, kColorMixerBandCount> luminance{};
    bool enabled = true;

    [[nodiscard]] bool isNeutral() const noexcept
    {
        if (!enabled) {
            return true;
        }

        const auto allZero = [](const auto &values) {
            return std::all_of(values.cbegin(), values.cend(),
                               [](int value) { return value == 0; });
        };
        return allZero(hue) && allZero(saturation) && allZero(luminance);
    }

    bool operator==(const ColorMixerSettings &other) const noexcept = default;
};

} // namespace imaging
