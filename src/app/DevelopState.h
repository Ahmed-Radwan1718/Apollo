#pragma once

#include "imaging/DetailSettings.h"
#include "imaging/RawCalibration.h"

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
    imaging::RawCalibration calibration;
    imaging::DetailSettings detail;

    // Geometry is stored non-destructively. Crop coordinates use a normalized
    // 0..100000 space relative to the final geometry canvas.
    int cropX = 0;
    int cropY = 0;
    int cropWidth = 100000;
    int cropHeight = 100000;
    int cropQuarterTurns = 0;
    int cropStraightenHundredths = 0;
    int cropAspectMode = 0;

    // Manual Transform controls are also geometry-only and are applied after
    // Crop/Rotate orientation and fine straighten, before the crop rectangle.
    // Upright modes write their solved correction into the same non-destructive
    // transform controls, while this mode value records which Upright option was
    // selected for UI/history restoration.
    int transformUprightMode = 0;
    int transformVertical = 0;
    int transformHorizontal = 0;
    int transformRotateHundredths = 0;
    int transformAspect = 0;
    int transformScale = 100;
    int transformXOffset = 0;
    int transformYOffset = 0;
    bool transformConstrainCrop = false;

    // Main develop-render comparisons intentionally cover the RAW-stage controls.
    // Detail is applied afterwards by TemperatureRenderService, and geometry is
    // applied afterwards by the geometry pipeline. fullyEquals() includes both.
    bool operator==(const DevelopState &other) const noexcept
    {
        return temperatureKelvin == other.temperatureKelvin
            && tint == other.tint
            && exposureHundredths == other.exposureHundredths
            && highlights == other.highlights
            && shadows == other.shadows
            && whites == other.whites
            && blacks == other.blacks
            && contrast == other.contrast
            && texture == other.texture
            && clarity == other.clarity
            && dehaze == other.dehaze
            && saturation == other.saturation
            && vibrance == other.vibrance
            && calibration == other.calibration;
    }

    [[nodiscard]] bool fullyEquals(const DevelopState &other) const noexcept
    {
        return *this == other
            && detail == other.detail
            && geometryEquals(other);
    }

    [[nodiscard]] bool geometryEquals(const DevelopState &other) const noexcept
    {
        return cropX == other.cropX
            && cropY == other.cropY
            && cropWidth == other.cropWidth
            && cropHeight == other.cropHeight
            && cropQuarterTurns == other.cropQuarterTurns
            && cropStraightenHundredths == other.cropStraightenHundredths
            && cropAspectMode == other.cropAspectMode
            && transformUprightMode == other.transformUprightMode
            && transformVertical == other.transformVertical
            && transformHorizontal == other.transformHorizontal
            && transformRotateHundredths == other.transformRotateHundredths
            && transformAspect == other.transformAspect
            && transformScale == other.transformScale
            && transformXOffset == other.transformXOffset
            && transformYOffset == other.transformYOffset
            && transformConstrainCrop == other.transformConstrainCrop;
    }
};
