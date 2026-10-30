#pragma once

namespace imaging {

struct EffectsSettings {
    int vignetteAmount = 0;
    int vignetteMidpoint = 50;
    int vignetteRoundness = 0;
    int vignetteFeather = 50;
    int vignetteHighlights = 0;

    int grainAmount = 0;
    int grainSize = 25;
    int grainRoughness = 50;

    [[nodiscard]] bool hasVisibleEffects() const noexcept
    {
        return vignetteAmount != 0 || grainAmount != 0;
    }

    bool operator==(const EffectsSettings &other) const noexcept = default;
};

} // namespace imaging
