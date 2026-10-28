#pragma once

#include <libraw/libraw.h>

#include <QMetaType>

namespace imaging {

struct RawCalibration {
    int shadowsTint = 0;
    int redPrimaryHue = 0;
    int redPrimarySaturation = 0;
    int greenPrimaryHue = 0;
    int greenPrimarySaturation = 0;
    int bluePrimaryHue = 0;
    int bluePrimarySaturation = 0;

    [[nodiscard]] bool isNeutral() const noexcept;
    [[nodiscard]] bool primariesAreNeutral() const noexcept;

    bool operator==(const RawCalibration &other) const noexcept = default;
};

// LibRaw performs white balance and demosaic before convert_to_rgb_loop().
// Overriding that conversion hook lets Apollo calibrate the camera primaries
// while the pixels are still camera RGB, before they become linear sRGB.
class CalibratedLibRaw final : public LibRaw
{
public:
    explicit CalibratedLibRaw(unsigned int flags = 0)
        : LibRaw(flags)
    {
    }

    void setRawCalibration(const RawCalibration &calibration) noexcept;
    [[nodiscard]] const RawCalibration &rawCalibration() const noexcept;

protected:
    void convert_to_rgb_loop(float outCamera[3][4]) override;

private:
    RawCalibration m_calibration;
};

} // namespace imaging

Q_DECLARE_METATYPE(imaging::RawCalibration)
