#pragma once

struct DevelopState {
    int temperatureKelvin = 0;
    int tint = 0;
    int exposureHundredths = 0;
    int highlights = 0;
    int shadows = 0;
    int whites = 0;
    int blacks = 0;
    int contrast = 0;
    int texture = 0;
    int clarity = 0;
    int dehaze = 0;
    int saturation = 0;
    int vibrance = 0;

    bool operator==(const DevelopState &) const = default;
};
