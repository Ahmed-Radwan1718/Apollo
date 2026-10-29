#pragma once

namespace imaging {

struct DetailSettings {
    int sharpeningAmount = 0;
    int sharpeningRadiusHundredths = 100;
    int sharpeningDetail = 25;
    int sharpeningMasking = 0;
    int luminanceNoiseReduction = 0;
    int colorNoiseReduction = 0;

    bool operator==(const DetailSettings &other) const noexcept = default;
};

} // namespace imaging
