#include "imaging/RawCalibration.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <numbers>

namespace imaging {
namespace {

constexpr double kLinearMaximum = 65535.0;
constexpr double kSceneMiddleGray = 0.18;
constexpr double kShadowTintStartEv = -0.5;
constexpr double kShadowTintFullEv = -3.0;
constexpr double kShadowTintMaximumLogShift = 0.30010459245033816; // ln(1.35)
constexpr double kMaximumPrimaryHueDegrees = 30.0;
constexpr double kLinearSrgbRedLuminance = 0.2126;
constexpr double kLinearSrgbGreenLuminance = 0.7152;
constexpr double kLinearSrgbBlueLuminance = 0.0722;

int clampedCalibrationValue(int value)
{
    return std::clamp(value, -100, 100);
}

double smoothstep01(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

double shadowTintWeight(double linearLuminance)
{
    if (!(linearLuminance > 0.0)) {
        return 1.0;
    }

    const double sceneEv = std::log2(linearLuminance / kSceneMiddleGray);
    if (sceneEv >= kShadowTintStartEv) {
        return 0.0;
    }
    if (sceneEv <= kShadowTintFullEv) {
        return 1.0;
    }

    const double normalized = (kShadowTintStartEv - sceneEv)
        / (kShadowTintStartEv - kShadowTintFullEv);
    return smoothstep01(normalized);
}

void adjustPrimaryColumn(float matrix[3][4],
                         int column,
                         int hue,
                         int saturation)
{
    const int clampedHue = clampedCalibrationValue(hue);
    const int clampedSaturation = clampedCalibrationValue(saturation);
    if (clampedHue == 0 && clampedSaturation == 0) {
        return;
    }

    const double red = matrix[0][column];
    const double green = matrix[1][column];
    const double blue = matrix[2][column];
    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;

    // A column of LibRaw's out_cam matrix describes where one camera primary
    // lands in the linear output space. Rotate and scale that column's chromatic
    // component while preserving luminance. This changes the camera-to-working
    // transform itself; it does not select already-rendered pixels by hue.
    const double redOpponent = red - luminance;
    const double blueOpponent = blue - luminance;
    const double angle = (static_cast<double>(clampedHue) / 100.0)
        * kMaximumPrimaryHueDegrees
        * std::numbers::pi_v<double> / 180.0;
    const double chromaScale = std::max(
        0.0, 1.0 + static_cast<double>(clampedSaturation) / 100.0);
    const double cosine = std::cos(angle);
    const double sine = std::sin(angle);
    const double rotatedRed = chromaScale
        * (cosine * redOpponent - sine * blueOpponent);
    const double rotatedBlue = chromaScale
        * (sine * redOpponent + cosine * blueOpponent);

    const double calibratedRed = luminance + rotatedRed;
    const double calibratedBlue = luminance + rotatedBlue;
    const double calibratedGreen =
        (luminance
         - kLinearSrgbRedLuminance * calibratedRed
         - kLinearSrgbBlueLuminance * calibratedBlue)
        / kLinearSrgbGreenLuminance;

    if (std::isfinite(calibratedRed)
        && std::isfinite(calibratedGreen)
        && std::isfinite(calibratedBlue)) {
        matrix[0][column] = static_cast<float>(calibratedRed);
        matrix[1][column] = static_cast<float>(calibratedGreen);
        matrix[2][column] = static_cast<float>(calibratedBlue);
    }
}

void calibratedMatrix(float source[3][4],
                      const RawCalibration &calibration,
                      float destination[3][4])
{
    std::memcpy(destination, source, sizeof(float) * 12);
    adjustPrimaryColumn(destination, 0,
                        calibration.redPrimaryHue,
                        calibration.redPrimarySaturation);
    adjustPrimaryColumn(destination, 1,
                        calibration.greenPrimaryHue,
                        calibration.greenPrimarySaturation);
    adjustPrimaryColumn(destination, 2,
                        calibration.bluePrimaryHue,
                        calibration.bluePrimarySaturation);

    // Four-channel camera RGB uses the second green channel in column 3.
    adjustPrimaryColumn(destination, 3,
                        calibration.greenPrimaryHue,
                        calibration.greenPrimarySaturation);
}

unsigned short clippedSample(double value)
{
    if (!std::isfinite(value)) {
        return 0;
    }

    return static_cast<unsigned short>(std::clamp(
        static_cast<int>(value), 0, 65535));
}

} // namespace

bool RawCalibration::isNeutral() const noexcept
{
    return shadowsTint == 0 && primariesAreNeutral();
}

bool RawCalibration::primariesAreNeutral() const noexcept
{
    return redPrimaryHue == 0
        && redPrimarySaturation == 0
        && greenPrimaryHue == 0
        && greenPrimarySaturation == 0
        && bluePrimaryHue == 0
        && bluePrimarySaturation == 0;
}

void CalibratedLibRaw::setRawCalibration(const RawCalibration &calibration) noexcept
{
    m_calibration = calibration;
    m_calibration.shadowsTint = clampedCalibrationValue(m_calibration.shadowsTint);
    m_calibration.redPrimaryHue = clampedCalibrationValue(m_calibration.redPrimaryHue);
    m_calibration.redPrimarySaturation =
        clampedCalibrationValue(m_calibration.redPrimarySaturation);
    m_calibration.greenPrimaryHue = clampedCalibrationValue(m_calibration.greenPrimaryHue);
    m_calibration.greenPrimarySaturation =
        clampedCalibrationValue(m_calibration.greenPrimarySaturation);
    m_calibration.bluePrimaryHue = clampedCalibrationValue(m_calibration.bluePrimaryHue);
    m_calibration.bluePrimarySaturation =
        clampedCalibrationValue(m_calibration.bluePrimarySaturation);
}

const RawCalibration &CalibratedLibRaw::rawCalibration() const noexcept
{
    return m_calibration;
}

void CalibratedLibRaw::convert_to_rgb_loop(float outCamera[3][4])
{
    // Keep Apollo's pre-Calibration rendering byte-for-byte on the neutral path.
    if (m_calibration.isNeutral()) {
        LibRaw::convert_to_rgb_loop(outCamera);
        return;
    }

    float matrix[3][4]{};
    calibratedMatrix(outCamera, m_calibration, matrix);

    // Primary calibration is entirely representable by LibRaw's camera-to-RGB
    // conversion matrix, so let LibRaw perform its normal conversion and
    // histogram accounting whenever Shadows Tint is neutral.
    if (m_calibration.shadowsTint == 0) {
        LibRaw::convert_to_rgb_loop(matrix);
        return;
    }

    libraw_internal_data_t *internal = get_internal_data_pointer();
    if (internal == nullptr
        || internal->internal_output_params.raw_color
        || (imgdata.idata.colors != 3 && imgdata.idata.colors != 4)) {
        LibRaw::convert_to_rgb_loop(matrix);
        return;
    }

    std::memset(internal->output_data.histogram,
                0,
                sizeof(int) * LIBRAW_HISTOGRAM_SIZE * 4);

    const int colors = imgdata.idata.colors;
    const double tintAmount =
        static_cast<double>(m_calibration.shadowsTint) / 100.0;
    unsigned short *pixel = imgdata.image[0];

    for (int row = 0; row < imgdata.sizes.height; ++row) {
        for (int column = 0; column < imgdata.sizes.width; ++column, pixel += 4) {
            std::array<double, 4> camera{
                static_cast<double>(pixel[0]),
                static_cast<double>(pixel[1]),
                static_cast<double>(pixel[2]),
                static_cast<double>(pixel[3]),
            };

            // Derive the mask from the uncalibrated camera-to-linear-sRGB matrix
            // so the tint amount depends on scene luminance, not on the color
            // calibration that is about to be applied.
            const double baselineRed = outCamera[0][0] * camera[0]
                + outCamera[0][1] * camera[1]
                + outCamera[0][2] * camera[2]
                + (colors == 4 ? outCamera[0][3] * camera[3] : 0.0);
            const double baselineGreen = outCamera[1][0] * camera[0]
                + outCamera[1][1] * camera[1]
                + outCamera[1][2] * camera[2]
                + (colors == 4 ? outCamera[1][3] * camera[3] : 0.0);
            const double baselineBlue = outCamera[2][0] * camera[0]
                + outCamera[2][1] * camera[1]
                + outCamera[2][2] * camera[2]
                + (colors == 4 ? outCamera[2][3] * camera[3] : 0.0);
            const double baselineLuminance = std::max(
                0.0,
                (baselineRed * kLinearSrgbRedLuminance
                 + baselineGreen * kLinearSrgbGreenLuminance
                 + baselineBlue * kLinearSrgbBlueLuminance)
                    / kLinearMaximum);
            const double weight = shadowTintWeight(baselineLuminance);

            if (weight > 0.0) {
                const double shift = tintAmount
                    * kShadowTintMaximumLogShift
                    * weight;
                const double redBlueGain = std::exp(0.5 * shift);
                const double greenGain = std::exp(-shift);
                camera[0] *= redBlueGain;
                camera[1] *= greenGain;
                camera[2] *= redBlueGain;
                if (colors == 4) {
                    camera[3] *= greenGain;
                }
            }

            const double red = matrix[0][0] * camera[0]
                + matrix[0][1] * camera[1]
                + matrix[0][2] * camera[2]
                + (colors == 4 ? matrix[0][3] * camera[3] : 0.0);
            const double green = matrix[1][0] * camera[0]
                + matrix[1][1] * camera[1]
                + matrix[1][2] * camera[2]
                + (colors == 4 ? matrix[1][3] * camera[3] : 0.0);
            const double blue = matrix[2][0] * camera[0]
                + matrix[2][1] * camera[1]
                + matrix[2][2] * camera[2]
                + (colors == 4 ? matrix[2][3] * camera[3] : 0.0);

            pixel[0] = clippedSample(red);
            pixel[1] = clippedSample(green);
            pixel[2] = clippedSample(blue);

            internal->output_data.histogram[0][pixel[0] >> 3]++;
            internal->output_data.histogram[1][pixel[1] >> 3]++;
            internal->output_data.histogram[2][pixel[2] >> 3]++;
            if (colors == 4) {
                internal->output_data.histogram[3][pixel[3] >> 3]++;
            }
        }
    }
}

} // namespace imaging
