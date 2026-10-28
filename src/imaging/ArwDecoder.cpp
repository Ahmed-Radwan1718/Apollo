#include "imaging/ArwDecoder.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QColorSpace>
#include <QFile>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <new>
#include <string>
#include <utility>
#include <vector>

namespace imaging {
namespace {

constexpr int kSonyTemperatureMinimumKelvin = 2000;
constexpr int kSonyTemperatureMaximumKelvin = 20000;
constexpr int kSonyTemperatureStepKelvin = 100;
constexpr int kTintMinimum = -150;
constexpr int kTintMaximum = 150;
constexpr double kTintMaximumLogShift = 0.6931471805599453; // ln(2)
constexpr int kExposureMinimumHundredths = -500;
constexpr int kExposureMaximumHundredths = 500;
constexpr int kHighlightsMinimum = -100;
constexpr int kHighlightsMaximum = 100;
constexpr double kSceneMiddleGray = 0.18;
constexpr double kHighlightsMaskStartEv = 0.5;
constexpr double kHighlightsMaskFullEv = 2.0;
constexpr double kHighlightsMaximumCompression = 0.9;
constexpr double kHighlightsMaximumExpansion = 0.9;
constexpr int kShadowsMinimum = -100;
constexpr int kShadowsMaximum = 100;
constexpr double kShadowsMaskStartEv = -1.0;
constexpr double kShadowsMaskFullEv = -3.0;
constexpr double kShadowsMaximumLiftCompression = 0.8;
constexpr double kShadowsMaximumDeepenExpansion = 0.5;
constexpr int kWhitesMinimum = -100;
constexpr int kWhitesMaximum = 100;
constexpr double kWhitesMaskStartEv = 1.75;
constexpr double kWhitesMaskFullEv = 2.75;
constexpr double kWhitesMaximumCompression = 0.9;
constexpr double kWhitesMaximumExpansion = 0.65;
constexpr int kBlacksMinimum = -100;
constexpr int kBlacksMaximum = 100;
constexpr double kBlacksMaskStartEv = -2.0;
constexpr double kBlacksMaskFullEv = -4.0;
constexpr double kBlacksMaximumLiftCompression = 0.9;
constexpr double kBlacksMaximumDeepenExpansion = 0.65;
constexpr int kContrastMinimum = -100;
constexpr int kContrastMaximum = 100;
constexpr double kContrastPivot = kSceneMiddleGray;
constexpr int kTextureMinimum = -100;
constexpr int kTextureMaximum = 100;
constexpr double kTextureReferenceShortSide = 4000.0;
constexpr double kTextureSmallRadiusAtReference = 1.0;
constexpr double kTextureLargeRadiusAtReference = 4.0;
constexpr double kTextureMaximumBandGain = 0.9;
constexpr double kTextureMaximumAdjustmentEv = 0.35;
constexpr double kTextureShadowProtectionStartEv = -5.0;
constexpr double kTextureShadowProtectionFullEv = -2.5;
constexpr double kTextureHighlightProtectionStartEv = 2.0;
constexpr double kTextureHighlightProtectionEndEv = 3.5;
constexpr int kClarityMinimum = -100;
constexpr int kClarityMaximum = 100;
constexpr double kClarityReferenceShortSide = 4000.0;
constexpr double kClaritySmallRadiusAtReference = 4.0;
constexpr double kClarityLargeRadiusAtReference = 24.0;
constexpr double kClarityMaximumBandGain = 1.05;
constexpr double kClarityBandSoftLimitEv = 0.60;
constexpr double kClarityMaximumAdjustmentEv = 0.45;
constexpr double kClarityShadowProtectionStartEv = -4.0;
constexpr double kClarityShadowProtectionFullEv = -1.5;
constexpr double kClarityHighlightProtectionStartEv = 1.0;
constexpr double kClarityHighlightProtectionEndEv = 2.75;
constexpr int kDehazeMinimum = -100;
constexpr int kDehazeMaximum = 100;
constexpr int kDehazeAirlightHistogramBins = 512;
constexpr double kDehazeAirlightPercentile = 0.995;
constexpr double kDehazeAirlightMinimumEv = -8.0;
constexpr double kDehazeAirlightMaximumEv = 8.0;
constexpr double kDehazeAirlightNeutralityStrength = 3.0;
constexpr double kDehazeAirlightChannelFloor = 0.10;
constexpr double kDehazeAirlightChannelCeiling = 4.0;
constexpr double kDehazePositiveMaximumRemoval = 0.72;
constexpr double kDehazeMinimumTransmission = 0.30;
constexpr double kDehazeNegativeMaximumVeil = 0.45;
constexpr double kDehazeShadowProtectionStartEv = -4.5;
constexpr double kDehazeShadowProtectionFullEv = -1.5;
constexpr double kDehazeHighlightProtectionStartEv = 1.5;
constexpr double kDehazeHighlightProtectionEndEv = 3.2;
constexpr int kSaturationMinimum = -100;
constexpr int kSaturationMaximum = 100;
constexpr int kVibranceMinimum = -100;
constexpr int kVibranceMaximum = 100;
constexpr int kLinearMaximum = 65535;
constexpr double kAutoBrightnessClipFraction = 0.01;
constexpr double kLinearSrgbRedLuminance = 0.2126;
constexpr double kLinearSrgbGreenLuminance = 0.7152;
constexpr double kLinearSrgbBlueLuminance = 0.0722;
constexpr double kOklabReferenceChroma = 0.25;
constexpr double kWarmHueCenterRadians = 0.8726646259971648; // 50 degrees
constexpr double kWarmHueHalfWidthRadians = 0.6108652381980153; // 35 degrees
constexpr double kWarmHueProtectionStrength = 0.5;
constexpr int kVibranceGamutSearchIterations = 8;
constexpr double kTwoPi = 6.28318530717958647692;

struct WhiteBalanceRatios {
    double redToGreen = 0.0;
    double blueToGreen = 0.0;
};

struct TemperatureCalibration {
    int kelvin = 0;
    WhiteBalanceRatios ratios;
};

struct LinearRgb {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;
};

struct Oklab {
    double lightness = 0.0;
    double a = 0.0;
    double b = 0.0;
};

QString libRawError(const char *stage, int errorCode)
{
    return QStringLiteral("%1 failed: %2")
        .arg(QString::fromLatin1(stage),
             QString::fromLatin1(LibRaw::strerror(errorCode)));
}

int openFile(LibRaw &processor, const QString &filePath)
{
#ifdef _WIN32
    const std::wstring widePath = filePath.toStdWString();
    const int wideResult = processor.open_file(widePath.c_str());
    if (wideResult != LIBRAW_NOT_IMPLEMENTED) {
        return wideResult;
    }
#endif

    const QByteArray encodedPath = QFile::encodeName(filePath);
    return processor.open_file(encodedPath.constData());
}

void setError(QString *error, QString message)
{
    if (error != nullptr) {
        *error = std::move(message);
    }
}

bool logarithmicWhiteBalanceRatios(const float *coefficients,
                                   WhiteBalanceRatios &ratios)
{
    const double red = coefficients[0];
    const double green1 = coefficients[1];
    const double blue = coefficients[2];
    const double green2 = coefficients[3];

    if (!(red > 0.0) || !(blue > 0.0)) {
        return false;
    }

    const double green = green1 > 0.0 ? green1 : green2;
    if (!(green > 0.0)) {
        return false;
    }

    ratios.redToGreen = std::log(red / green);
    ratios.blueToGreen = std::log(blue / green);
    return std::isfinite(ratios.redToGreen) && std::isfinite(ratios.blueToGreen);
}

std::vector<TemperatureCalibration> sonyTemperatureCalibrations(
    const libraw_colordata_t &color)
{
    std::vector<TemperatureCalibration> calibrations;
    calibrations.reserve(64);

    for (const auto &row : color.WBCT_Coeffs) {
        const int kelvin = static_cast<int>(std::lround(row[0]));
        if (kelvin < kSonyTemperatureMinimumKelvin
            || kelvin > kSonyTemperatureMaximumKelvin) {
            continue;
        }

        WhiteBalanceRatios ratios;
        if (!logarithmicWhiteBalanceRatios(&row[1], ratios)) {
            continue;
        }

        calibrations.push_back({kelvin, ratios});
    }

    std::sort(calibrations.begin(), calibrations.end(),
              [](const TemperatureCalibration &left,
                 const TemperatureCalibration &right) {
                  return left.kelvin < right.kelvin;
              });

    calibrations.erase(
        std::unique(calibrations.begin(), calibrations.end(),
                    [](const TemperatureCalibration &left,
                       const TemperatureCalibration &right) {
                        return left.kelvin == right.kelvin;
                    }),
        calibrations.end());

    return calibrations;
}

bool whiteBalanceRatiosAtKelvin(const std::vector<TemperatureCalibration> &calibrations,
                                int kelvin,
                                WhiteBalanceRatios &ratios)
{
    if (calibrations.size() < 2 || kelvin <= 0) {
        return false;
    }

    const auto upper = std::lower_bound(
        calibrations.begin(), calibrations.end(), kelvin,
        [](const TemperatureCalibration &point, int value) {
            return point.kelvin < value;
        });

    std::size_t lowerIndex = 0;
    std::size_t upperIndex = 1;

    if (upper == calibrations.begin()) {
        lowerIndex = 0;
        upperIndex = 1;
    } else if (upper == calibrations.end()) {
        lowerIndex = calibrations.size() - 2;
        upperIndex = calibrations.size() - 1;
    } else {
        upperIndex = static_cast<std::size_t>(upper - calibrations.begin());
        lowerIndex = upperIndex - 1;
    }

    const TemperatureCalibration &lower = calibrations[lowerIndex];
    const TemperatureCalibration &higher = calibrations[upperIndex];

    const double lowerMired = 1000000.0 / static_cast<double>(lower.kelvin);
    const double higherMired = 1000000.0 / static_cast<double>(higher.kelvin);
    const double targetMired = 1000000.0 / static_cast<double>(kelvin);
    const double denominator = higherMired - lowerMired;

    if (std::abs(denominator) <= std::numeric_limits<double>::epsilon()) {
        return false;
    }

    const double blend = (targetMired - lowerMired) / denominator;
    ratios.redToGreen = lower.ratios.redToGreen
        + blend * (higher.ratios.redToGreen - lower.ratios.redToGreen);
    ratios.blueToGreen = lower.ratios.blueToGreen
        + blend * (higher.ratios.blueToGreen - lower.ratios.blueToGreen);

    return std::isfinite(ratios.redToGreen) && std::isfinite(ratios.blueToGreen);
}

int sonyWhiteBalanceTemperatureKelvin(
    const libraw_colordata_t &color,
    const std::vector<TemperatureCalibration> &calibrations)
{
    WhiteBalanceRatios asShotRatios;
    if (!logarithmicWhiteBalanceRatios(color.cam_mul, asShotRatios)
        || calibrations.size() < 2) {
        return 0;
    }

    int bestKelvin = 0;
    double bestDistance = std::numeric_limits<double>::infinity();

    for (int kelvin = kSonyTemperatureMinimumKelvin;
         kelvin <= kSonyTemperatureMaximumKelvin;
         kelvin += kSonyTemperatureStepKelvin) {
        WhiteBalanceRatios predicted;
        if (!whiteBalanceRatiosAtKelvin(calibrations, kelvin, predicted)) {
            continue;
        }

        const double redError = asShotRatios.redToGreen - predicted.redToGreen;
        const double blueError = asShotRatios.blueToGreen - predicted.blueToGreen;
        const double distance = redError * redError + blueError * blueError;

        if (distance < bestDistance) {
            bestDistance = distance;
            bestKelvin = kelvin;
        }
    }

    return bestKelvin;
}

double tintLogShift(int tint)
{
    const int clampedTint = std::clamp(tint, kTintMinimum, kTintMaximum);
    return (static_cast<double>(clampedTint) / static_cast<double>(kTintMaximum))
        * kTintMaximumLogShift;
}

bool sonyWhiteBalanceMultipliersForDevelopState(
    const libraw_colordata_t &color,
    const std::vector<TemperatureCalibration> &calibrations,
    int cameraKelvin,
    int targetKelvin,
    int tint,
    float (&multipliers)[4])
{
    WhiteBalanceRatios asShotRatios;
    WhiteBalanceRatios cameraCurveRatios;
    WhiteBalanceRatios targetCurveRatios;

    if (!logarithmicWhiteBalanceRatios(color.cam_mul, asShotRatios)
        || !whiteBalanceRatiosAtKelvin(calibrations, cameraKelvin, cameraCurveRatios)
        || !whiteBalanceRatiosAtKelvin(calibrations, targetKelvin, targetCurveRatios)) {
        return false;
    }

    // Move along Sony's own temperature calibration curve while retaining the
    // as-shot offset from that curve, then apply Tint independently in the same
    // log-ratio space so green/magenta remains a pre-demosaic RAW adjustment.
    const double tintShift = tintLogShift(tint);
    const double redToGreen = targetCurveRatios.redToGreen
        + (asShotRatios.redToGreen - cameraCurveRatios.redToGreen)
        + tintShift;
    const double blueToGreen = targetCurveRatios.blueToGreen
        + (asShotRatios.blueToGreen - cameraCurveRatios.blueToGreen)
        + tintShift;

    const double red = std::exp(redToGreen);
    const double blue = std::exp(blueToGreen);
    const double green2 = color.cam_mul[1] > 0.0f && color.cam_mul[3] > 0.0f
        ? static_cast<double>(color.cam_mul[3] / color.cam_mul[1])
        : 1.0;

    if (!std::isfinite(red) || !std::isfinite(blue) || !std::isfinite(green2)
        || !(red > 0.0) || !(blue > 0.0) || !(green2 > 0.0)) {
        return false;
    }

    multipliers[0] = static_cast<float>(red);
    multipliers[1] = 1.0f;
    multipliers[2] = static_cast<float>(blue);
    multipliers[3] = static_cast<float>(green2);
    return true;
}

bool sonyWhiteBalanceValuesForMultipliers(
    const libraw_colordata_t &color,
    const std::vector<TemperatureCalibration> &calibrations,
    int cameraKelvin,
    const float *targetMultipliers,
    int &temperatureKelvin,
    int &tint)
{
    WhiteBalanceRatios asShotRatios;
    WhiteBalanceRatios cameraCurveRatios;
    WhiteBalanceRatios targetRatios;

    if (cameraKelvin <= 0
        || !logarithmicWhiteBalanceRatios(color.cam_mul, asShotRatios)
        || !whiteBalanceRatiosAtKelvin(calibrations, cameraKelvin, cameraCurveRatios)
        || !logarithmicWhiteBalanceRatios(targetMultipliers, targetRatios)) {
        return false;
    }

    const double redOffset = asShotRatios.redToGreen - cameraCurveRatios.redToGreen;
    const double blueOffset = asShotRatios.blueToGreen - cameraCurveRatios.blueToGreen;

    double bestError = std::numeric_limits<double>::infinity();
    double bestTintShift = 0.0;
    int bestKelvin = 0;

    for (int kelvin = kSonyTemperatureMinimumKelvin;
         kelvin <= kSonyTemperatureMaximumKelvin;
         ++kelvin) {
        WhiteBalanceRatios curveRatios;
        if (!whiteBalanceRatiosAtKelvin(calibrations, kelvin, curveRatios)) {
            continue;
        }

        const double baseRed = curveRatios.redToGreen + redOffset;
        const double baseBlue = curveRatios.blueToGreen + blueOffset;
        const double requestedShift = 0.5
            * ((targetRatios.redToGreen - baseRed)
               + (targetRatios.blueToGreen - baseBlue));
        const double shift = std::clamp(requestedShift,
                                        -kTintMaximumLogShift,
                                        kTintMaximumLogShift);

        const double redError = targetRatios.redToGreen - (baseRed + shift);
        const double blueError = targetRatios.blueToGreen - (baseBlue + shift);
        const double error = redError * redError + blueError * blueError;

        if (error < bestError) {
            bestError = error;
            bestKelvin = kelvin;
            bestTintShift = shift;
        }
    }

    if (bestKelvin <= 0 || !std::isfinite(bestError)) {
        return false;
    }

    temperatureKelvin = bestKelvin;
    tint = std::clamp(
        static_cast<int>(std::lround(
            (bestTintShift / kTintMaximumLogShift) * kTintMaximum)),
        kTintMinimum,
        kTintMaximum);
    return true;
}

QPointF normalizedPointBeforeRotation(const QPointF &point, int flip)
{
    const qreal x = std::clamp(point.x(), 0.0, 1.0);
    const qreal y = std::clamp(point.y(), 0.0, 1.0);

    switch (flip) {
    case 3:
    case 180:
        return QPointF(1.0 - x, 1.0 - y);
    case 5:
    case 270:
    case -90:
        return QPointF(1.0 - y, x);
    case 6:
    case 90:
        return QPointF(y, 1.0 - x);
    default:
        return QPointF(x, y);
    }
}

bool configureGreyBox(LibRaw &processor, const QPointF &normalizedPosition)
{
    if (!std::isfinite(normalizedPosition.x())
        || !std::isfinite(normalizedPosition.y())) {
        return false;
    }

    const int width = processor.imgdata.sizes.width;
    const int height = processor.imgdata.sizes.height;
    if (width <= 0 || height <= 0) {
        return false;
    }

    const QPointF sourcePoint = normalizedPointBeforeRotation(
        normalizedPosition, processor.imgdata.sizes.flip);

    int sampleSide = std::clamp(std::min(width, height) / 300, 8, 32);
    if ((sampleSide & 1) != 0) {
        ++sampleSide;
    }
    sampleSide = std::min(sampleSide, std::min(width, height));
    if (sampleSide < 4) {
        return false;
    }

    const int centerX = std::clamp(
        static_cast<int>(std::lround(sourcePoint.x() * (width - 1))),
        0,
        width - 1);
    const int centerY = std::clamp(
        static_cast<int>(std::lround(sourcePoint.y() * (height - 1))),
        0,
        height - 1);

    const int x = std::clamp(centerX - sampleSide / 2, 0, width - sampleSide);
    const int y = std::clamp(centerY - sampleSide / 2, 0, height - sampleSide);

    processor.imgdata.params.greybox[0] = static_cast<unsigned>(x);
    processor.imgdata.params.greybox[1] = static_cast<unsigned>(y);
    processor.imgdata.params.greybox[2] = static_cast<unsigned>(sampleSide);
    processor.imgdata.params.greybox[3] = static_cast<unsigned>(sampleSide);
    return true;
}

int autoBrightnessWhitePoint(const std::uint16_t *samples, std::size_t pixelCount)
{
    if (samples == nullptr || pixelCount == 0) {
        return kLinearMaximum;
    }

    std::vector<std::uint32_t> histogram(kLinearMaximum + 1, 0);
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const std::size_t offset = pixel * 3;
        const std::uint16_t maximumChannel = std::max(
            samples[offset], std::max(samples[offset + 1], samples[offset + 2]));
        ++histogram[maximumChannel];
    }

    const std::uint64_t targetCount = static_cast<std::uint64_t>(std::ceil(
        static_cast<double>(pixelCount) * (1.0 - kAutoBrightnessClipFraction)));
    std::uint64_t cumulative = 0;
    for (int value = 0; value <= kLinearMaximum; ++value) {
        cumulative += histogram[static_cast<std::size_t>(value)];
        if (cumulative >= targetCount) {
            return std::max(1, value);
        }
    }

    return kLinearMaximum;
}

std::uint8_t linearToSrgb8(double linear)
{
    linear = std::clamp(linear, 0.0, 1.0);
    const double encoded = linear <= 0.0031308
        ? 12.92 * linear
        : 1.055 * std::pow(linear, 1.0 / 2.4) - 0.055;
    return static_cast<std::uint8_t>(std::clamp(
        static_cast<int>(std::lround(encoded * 255.0)), 0, 255));
}

double contrastToneValue(double luminance, int contrast)
{
    const int clampedContrast = std::clamp(contrast,
                                           kContrastMinimum,
                                           kContrastMaximum);
    if (clampedContrast == 0 || !(luminance > 0.0) || luminance >= 1.0) {
        return luminance;
    }

    const double exponent = std::exp2(static_cast<double>(clampedContrast) / 100.0);
    if (luminance < kContrastPivot) {
        return kContrastPivot
            * std::pow(luminance / kContrastPivot, exponent);
    }

    return 1.0 - (1.0 - kContrastPivot)
        * std::pow((1.0 - luminance) / (1.0 - kContrastPivot), exponent);
}

void applyLinearContrast(double &red,
                         double &green,
                         double &blue,
                         int contrast)
{
    if (contrast == 0) {
        return;
    }

    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    const double mappedLuminance = contrastToneValue(luminance, contrast);
    const double scale = mappedLuminance / luminance;
    red *= scale;
    green *= scale;
    blue *= scale;
}

double saturationChromaScale(int saturation)
{
    const int clampedSaturation = std::clamp(saturation,
                                             kSaturationMinimum,
                                             kSaturationMaximum);
    return 1.0 + static_cast<double>(clampedSaturation) / 100.0;
}

void applyLinearSaturation(double &red,
                           double &green,
                           double &blue,
                           double chromaScale)
{
    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;

    red = luminance + (red - luminance) * chromaScale;
    green = luminance + (green - luminance) * chromaScale;
    blue = luminance + (blue - luminance) * chromaScale;
}

double smoothstep01(double value)
{
    const double clamped = std::clamp(value, 0.0, 1.0);
    return clamped * clamped * (3.0 - 2.0 * clamped);
}

double integratedHighlightWeightEv(double sceneEv)
{
    if (sceneEv <= kHighlightsMaskStartEv) {
        return 0.0;
    }

    const double width = kHighlightsMaskFullEv - kHighlightsMaskStartEv;
    if (sceneEv >= kHighlightsMaskFullEv) {
        return 0.5 * width + (sceneEv - kHighlightsMaskFullEv);
    }

    const double x = (sceneEv - kHighlightsMaskStartEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

void applyLinearHighlights(double &red,
                           double &green,
                           double &blue,
                           int highlights)
{
    const int clampedHighlights = std::clamp(highlights,
                                              kHighlightsMinimum,
                                              kHighlightsMaximum);
    if (clampedHighlights == 0) {
        return;
    }

    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    // Work in scene-referred EV relative to 18% middle gray. The integrated
    // smoothstep changes the slope of the highlight response instead of directly
    // subtracting brightness, which keeps the tone curve monotonic and avoids
    // reversals while smoothly rolling highlights in or out above +0.5 EV.
    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double weightedEv = integratedHighlightWeightEv(sceneEv);
    if (weightedEv <= 0.0) {
        return;
    }

    const double amount = std::abs(static_cast<double>(clampedHighlights)) / 100.0;
    const double slopeChange = clampedHighlights < 0
        ? -kHighlightsMaximumCompression * amount
        : kHighlightsMaximumExpansion * amount;
    const double gain = std::exp2(slopeChange * weightedEv);

    // Apply the same gain to every channel so highlight recovery changes scene
    // exposure without introducing hue shifts.
    red *= gain;
    green *= gain;
    blue *= gain;
}

double integratedShadowWeightEv(double sceneEv)
{
    if (sceneEv >= kShadowsMaskStartEv) {
        return 0.0;
    }

    const double width = kShadowsMaskStartEv - kShadowsMaskFullEv;
    if (sceneEv <= kShadowsMaskFullEv) {
        return 0.5 * width + (kShadowsMaskFullEv - sceneEv);
    }

    const double x = (kShadowsMaskStartEv - sceneEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

void applyLinearShadows(double &red,
                        double &green,
                        double &blue,
                        int shadows)
{
    const int clampedShadows = std::clamp(shadows,
                                           kShadowsMinimum,
                                           kShadowsMaximum);
    if (clampedShadows == 0) {
        return;
    }

    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    // Mirror the scene-referred highlight curve below middle gray. The control
    // begins at -1 EV and reaches full influence by -3 EV. Integrating the
    // smoothstep changes the shadow slope continuously, so even at +/-100 the
    // tone mapping remains monotonic instead of folding dark values over each other.
    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double weightedEv = integratedShadowWeightEv(sceneEv);
    if (weightedEv <= 0.0) {
        return;
    }

    const double amount = std::abs(static_cast<double>(clampedShadows)) / 100.0;
    const double slopeChange = clampedShadows > 0
        ? kShadowsMaximumLiftCompression * amount
        : -kShadowsMaximumDeepenExpansion * amount;
    const double gain = std::exp2(slopeChange * weightedEv);

    // Use one linear-light gain for all channels so lifting or deepening shadows
    // preserves the pixel's chromatic ratios instead of introducing a color cast.
    red *= gain;
    green *= gain;
    blue *= gain;
}

double integratedWhitesWeightEv(double sceneEv)
{
    if (sceneEv <= kWhitesMaskStartEv) {
        return 0.0;
    }

    const double width = kWhitesMaskFullEv - kWhitesMaskStartEv;
    if (sceneEv >= kWhitesMaskFullEv) {
        return 0.5 * width + (sceneEv - kWhitesMaskFullEv);
    }

    const double x = (sceneEv - kWhitesMaskStartEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

void applyLinearWhites(double &red,
                       double &green,
                       double &blue,
                       int whites)
{
    const int clampedWhites = std::clamp(whites,
                                          kWhitesMinimum,
                                          kWhitesMaximum);
    if (clampedWhites == 0) {
        return;
    }

    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    // Whites is a narrower upper-tone control than Highlights. It starts near
    // the display-white region (+1.75 EV above middle gray) and reaches full
    // influence by +2.75 EV. Changing slope in EV space moves the white point
    // without adding a flat brightness offset, while keeping the mapping monotonic.
    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double weightedEv = integratedWhitesWeightEv(sceneEv);
    if (weightedEv <= 0.0) {
        return;
    }

    const double amount = std::abs(static_cast<double>(clampedWhites)) / 100.0;
    const double slopeChange = clampedWhites < 0
        ? -kWhitesMaximumCompression * amount
        : kWhitesMaximumExpansion * amount;
    const double gain = std::exp2(slopeChange * weightedEv);

    // Preserve channel ratios so the white-point adjustment does not create a hue shift.
    red *= gain;
    green *= gain;
    blue *= gain;
}

double integratedBlacksWeightEv(double sceneEv)
{
    if (sceneEv >= kBlacksMaskStartEv) {
        return 0.0;
    }

    const double width = kBlacksMaskStartEv - kBlacksMaskFullEv;
    if (sceneEv <= kBlacksMaskFullEv) {
        return 0.5 * width + (kBlacksMaskFullEv - sceneEv);
    }

    const double x = (kBlacksMaskStartEv - sceneEv) / width;
    return width * (x * x * x - 0.5 * x * x * x * x);
}

void applyLinearBlacks(double &red,
                       double &green,
                       double &blue,
                       int blacks)
{
    const int clampedBlacks = std::clamp(blacks,
                                          kBlacksMinimum,
                                          kBlacksMaximum);
    if (clampedBlacks == 0) {
        return;
    }

    const double luminance = red * kLinearSrgbRedLuminance
        + green * kLinearSrgbGreenLuminance
        + blue * kLinearSrgbBlueLuminance;
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return;
    }

    // Blacks is narrower and deeper than Shadows. It starts below -2 EV from
    // 18% middle gray and reaches full influence by -4 EV, so it reshapes the
    // black-point region without turning into a broad exposure or shadow control.
    // The integrated smoothstep changes tonal slope continuously and keeps the
    // mapping monotonic even at the ends of the slider range.
    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double weightedEv = integratedBlacksWeightEv(sceneEv);
    if (weightedEv <= 0.0) {
        return;
    }

    const double amount = std::abs(static_cast<double>(clampedBlacks)) / 100.0;
    const double slopeChange = clampedBlacks > 0
        ? kBlacksMaximumLiftCompression * amount
        : -kBlacksMaximumDeepenExpansion * amount;
    const double gain = std::exp2(slopeChange * weightedEv);

    // Apply a common linear-light gain so setting the black point preserves RGB
    // ratios instead of deliberately introducing a hue shift in dark colors.
    red *= gain;
    green *= gain;
    blue *= gain;
}

double linearLuminance(const LinearRgb &rgb)
{
    return rgb.red * kLinearSrgbRedLuminance
        + rgb.green * kLinearSrgbGreenLuminance
        + rgb.blue * kLinearSrgbBlueLuminance;
}

LinearRgb toneAdjustedLinearRgb(const std::uint16_t *samples,
                                std::size_t source,
                                double sampleScale,
                                int highlights,
                                int shadows,
                                int whites,
                                int blacks,
                                int contrast)
{
    LinearRgb rgb{
        static_cast<double>(samples[source]) * sampleScale,
        static_cast<double>(samples[source + 1]) * sampleScale,
        static_cast<double>(samples[source + 2]) * sampleScale,
    };

    if (highlights != 0) {
        applyLinearHighlights(rgb.red, rgb.green, rgb.blue, highlights);
    }
    if (shadows != 0) {
        applyLinearShadows(rgb.red, rgb.green, rgb.blue, shadows);
    }
    if (whites != 0) {
        applyLinearWhites(rgb.red, rgb.green, rgb.blue, whites);
    }
    if (blacks != 0) {
        applyLinearBlacks(rgb.red, rgb.green, rgb.blue, blacks);
    }
    if (contrast != 0) {
        applyLinearContrast(rgb.red, rgb.green, rgb.blue, contrast);
    }

    return rgb;
}

void horizontalBoxBlur(const std::vector<float> &source,
                       std::vector<float> &destination,
                       int width,
                       int height,
                       int radius)
{
    for (int y = 0; y < height; ++y) {
        const std::size_t row = static_cast<std::size_t>(y) * width;
        int left = 0;
        int right = std::min(width - 1, radius);
        int count = right + 1;
        double sum = 0.0;
        for (int x = left; x <= right; ++x) {
            sum += source[row + static_cast<std::size_t>(x)];
        }

        for (int x = 0; x < width; ++x) {
            destination[row + static_cast<std::size_t>(x)] =
                static_cast<float>(sum / static_cast<double>(count));

            const int remove = x - radius;
            const int add = x + radius + 1;
            if (remove >= 0) {
                sum -= source[row + static_cast<std::size_t>(remove)];
                --count;
            }
            if (add < width) {
                sum += source[row + static_cast<std::size_t>(add)];
                ++count;
            }
        }
    }
}

void verticalBoxBlur(const std::vector<float> &source,
                     std::vector<float> &destination,
                     int width,
                     int height,
                     int radius)
{
    for (int x = 0; x < width; ++x) {
        int top = 0;
        int bottom = std::min(height - 1, radius);
        int count = bottom + 1;
        double sum = 0.0;
        for (int y = top; y <= bottom; ++y) {
            sum += source[static_cast<std::size_t>(y) * width + x];
        }

        for (int y = 0; y < height; ++y) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            destination[index] = static_cast<float>(sum / static_cast<double>(count));

            const int remove = y - radius;
            const int add = y + radius + 1;
            if (remove >= 0) {
                sum -= source[static_cast<std::size_t>(remove) * width + x];
                --count;
            }
            if (add < height) {
                sum += source[static_cast<std::size_t>(add) * width + x];
                ++count;
            }
        }
    }
}

void verticalBoxBlurDifference(const std::vector<float> &source,
                               std::vector<float> &smallAndBand,
                               int width,
                               int height,
                               int radius)
{
    for (int x = 0; x < width; ++x) {
        int top = 0;
        int bottom = std::min(height - 1, radius);
        int count = bottom + 1;
        double sum = 0.0;
        for (int y = top; y <= bottom; ++y) {
            sum += source[static_cast<std::size_t>(y) * width + x];
        }

        for (int y = 0; y < height; ++y) {
            const std::size_t index = static_cast<std::size_t>(y) * width + x;
            const double largeScale = sum / static_cast<double>(count);
            smallAndBand[index] = static_cast<float>(
                static_cast<double>(smallAndBand[index]) - largeScale);

            const int remove = y - radius;
            const int add = y + radius + 1;
            if (remove >= 0) {
                sum -= source[static_cast<std::size_t>(remove) * width + x];
                --count;
            }
            if (add < height) {
                sum += source[static_cast<std::size_t>(add) * width + x];
                ++count;
            }
        }
    }
}

std::pair<int, int> textureRadii(int width, int height)
{
    const int shortSide = std::max(1, std::min(width, height));
    const double scale = static_cast<double>(shortSide) / kTextureReferenceShortSide;
    const int smallRadius = std::clamp(
        static_cast<int>(std::lround(kTextureSmallRadiusAtReference * scale)), 1, 3);
    const int largeRadius = std::clamp(
        static_cast<int>(std::lround(kTextureLargeRadiusAtReference * scale)),
        smallRadius + 1,
        12);
    return {smallRadius, largeRadius};
}

bool buildTextureBand(const std::uint16_t *samples,
                      std::size_t pixelCount,
                      int width,
                      int height,
                      double sampleScale,
                      int highlights,
                      int shadows,
                      int whites,
                      int blacks,
                      int contrast,
                      std::vector<float> &textureBand,
                      std::vector<float> &scratch,
                      QString *error)
{
    try {
        textureBand.resize(pixelCount);
        scratch.resize(pixelCount);
    } catch (const std::bad_alloc &) {
        setError(error, QStringLiteral("Not enough memory to calculate the RAW texture adjustment."));
        return false;
    }

    constexpr double minimumLuminance = 1e-8;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const LinearRgb rgb = toneAdjustedLinearRgb(samples,
                                                    pixel * 3,
                                                    sampleScale,
                                                    highlights,
                                                    shadows,
                                                    whites,
                                                    blacks,
                                                    contrast);
        textureBand[pixel] = static_cast<float>(
            std::log2(std::max(linearLuminance(rgb), minimumLuminance)));
    }

    const auto [smallRadius, largeRadius] = textureRadii(width, height);

    // A difference of two compact, resolution-scaled blurs isolates the
    // mid-spatial-frequency structure associated with texture. Blurring once at
    // the fine scale also suppresses single-pixel noise before the band is formed.
    horizontalBoxBlur(textureBand, scratch, width, height, smallRadius);
    verticalBoxBlur(scratch, textureBand, width, height, smallRadius);
    horizontalBoxBlur(textureBand, scratch, width, height, largeRadius);
    verticalBoxBlurDifference(scratch, textureBand, width, height, largeRadius);
    return true;
}

double textureToneWeight(double luminance)
{
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }

    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double shadowWeight = smoothstep01(
        (sceneEv - kTextureShadowProtectionStartEv)
        / (kTextureShadowProtectionFullEv - kTextureShadowProtectionStartEv));
    const double highlightWeight = 1.0 - smoothstep01(
        (sceneEv - kTextureHighlightProtectionStartEv)
        / (kTextureHighlightProtectionEndEv - kTextureHighlightProtectionStartEv));
    return shadowWeight * highlightWeight;
}

void applyLinearTexture(double &red,
                        double &green,
                        double &blue,
                        int texture,
                        float bandEv)
{
    const int clampedTexture = std::clamp(texture,
                                           kTextureMinimum,
                                           kTextureMaximum);
    if (clampedTexture == 0) {
        return;
    }

    const LinearRgb rgb{red, green, blue};
    const double luminance = linearLuminance(rgb);
    const double toneWeight = textureToneWeight(luminance);
    if (toneWeight <= 0.0) {
        return;
    }

    const double amount = static_cast<double>(clampedTexture) / 100.0;
    const double requestedAdjustmentEv = amount
        * kTextureMaximumBandGain
        * static_cast<double>(bandEv)
        * toneWeight;
    const double adjustmentEv = std::clamp(requestedAdjustmentEv,
                                            -kTextureMaximumAdjustmentEv,
                                            kTextureMaximumAdjustmentEv);
    const double gain = std::exp2(adjustmentEv);

    // Texture is derived only from luminance, then applied as one gain to all
    // channels. Positive values increase mid-frequency local contrast; negative
    // values reduce it, without changing the pixel's RGB ratios or global tone.
    red *= gain;
    green *= gain;
    blue *= gain;
}

std::pair<int, int> clarityRadii(int width, int height)
{
    const int shortSide = std::max(1, std::min(width, height));
    const double scale = static_cast<double>(shortSide) / kClarityReferenceShortSide;
    const int smallRadius = std::clamp(
        static_cast<int>(std::lround(kClaritySmallRadiusAtReference * scale)), 2, 8);
    const int largeRadius = std::clamp(
        static_cast<int>(std::lround(kClarityLargeRadiusAtReference * scale)),
        smallRadius + 4,
        64);
    return {smallRadius, largeRadius};
}

bool buildClarityBand(const std::uint16_t *samples,
                      std::size_t pixelCount,
                      int width,
                      int height,
                      double sampleScale,
                      int highlights,
                      int shadows,
                      int whites,
                      int blacks,
                      int contrast,
                      std::vector<float> &clarityBand,
                      std::vector<float> &scratch,
                      QString *error)
{
    try {
        clarityBand.resize(pixelCount);
        scratch.resize(pixelCount);
    } catch (const std::bad_alloc &) {
        setError(error, QStringLiteral("Not enough memory to calculate the RAW clarity adjustment."));
        return false;
    }

    constexpr double minimumLuminance = 1e-8;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const LinearRgb rgb = toneAdjustedLinearRgb(samples,
                                                    pixel * 3,
                                                    sampleScale,
                                                    highlights,
                                                    shadows,
                                                    whites,
                                                    blacks,
                                                    contrast);
        clarityBand[pixel] = static_cast<float>(
            std::log2(std::max(linearLuminance(rgb), minimumLuminance)));
    }

    const auto [smallRadius, largeRadius] = clarityRadii(width, height);

    // Clarity works at a deliberately broader scale than Texture. The first
    // blur removes fine texture and sensor noise; subtracting a much wider local
    // base then isolates medium/large scene structure for local-contrast shaping.
    horizontalBoxBlur(clarityBand, scratch, width, height, smallRadius);
    verticalBoxBlur(scratch, clarityBand, width, height, smallRadius);
    horizontalBoxBlur(clarityBand, scratch, width, height, largeRadius);
    verticalBoxBlurDifference(scratch, clarityBand, width, height, largeRadius);
    return true;
}

double clarityToneWeight(double luminance)
{
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }

    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double shadowWeight = smoothstep01(
        (sceneEv - kClarityShadowProtectionStartEv)
        / (kClarityShadowProtectionFullEv - kClarityShadowProtectionStartEv));
    const double highlightWeight = 1.0 - smoothstep01(
        (sceneEv - kClarityHighlightProtectionStartEv)
        / (kClarityHighlightProtectionEndEv - kClarityHighlightProtectionStartEv));
    return shadowWeight * highlightWeight;
}

void applyLinearClarity(double &red,
                        double &green,
                        double &blue,
                        int clarity,
                        float bandEv)
{
    const int clampedClarity = std::clamp(clarity,
                                           kClarityMinimum,
                                           kClarityMaximum);
    if (clampedClarity == 0) {
        return;
    }

    const LinearRgb rgb{red, green, blue};
    const double luminance = linearLuminance(rgb);
    const double toneWeight = clarityToneWeight(luminance);
    if (toneWeight <= 0.0) {
        return;
    }

    // Soft-limit unusually strong local edges before scaling them. This keeps
    // broad local contrast responsive while preventing extreme dark/light halos
    // from growing linearly at high Clarity settings.
    const double rawBandEv = static_cast<double>(bandEv);
    const double compressedBandEv = kClarityBandSoftLimitEv
        * std::tanh(rawBandEv / kClarityBandSoftLimitEv);
    const double amount = static_cast<double>(clampedClarity) / 100.0;
    const double requestedAdjustmentEv = amount
        * kClarityMaximumBandGain
        * compressedBandEv
        * toneWeight;
    const double adjustmentEv = std::clamp(requestedAdjustmentEv,
                                            -kClarityMaximumAdjustmentEv,
                                            kClarityMaximumAdjustmentEv);
    const double gain = std::exp2(adjustmentEv);

    // The local structure is measured from luminance only, then applied as one
    // linear-light gain to RGB. Positive Clarity expands broader local contrast;
    // negative Clarity compresses it without deliberately rotating hue.
    red *= gain;
    green *= gain;
    blue *= gain;
}

LinearRgb estimateDehazeAirlight(const std::uint16_t *samples,
                                 std::size_t pixelCount,
                                 double sampleScale,
                                 int highlights,
                                 int shadows,
                                 int whites,
                                 int blacks,
                                 int contrast)
{
    std::array<std::uint64_t, kDehazeAirlightHistogramBins> histogram{};
    constexpr double minimumLuminance = 1e-8;
    const double histogramEvRange =
        kDehazeAirlightMaximumEv - kDehazeAirlightMinimumEv;

    auto histogramBinForLuminance = [histogramEvRange, minimumLuminance](double luminance) {
        const double sceneEv = std::log2(
            std::max(luminance, minimumLuminance) / kSceneMiddleGray);
        const double normalized = std::clamp(
            (sceneEv - kDehazeAirlightMinimumEv) / histogramEvRange,
            0.0,
            1.0);
        return std::min(
            kDehazeAirlightHistogramBins - 1,
            static_cast<int>(normalized * kDehazeAirlightHistogramBins));
    };

    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const LinearRgb rgb = toneAdjustedLinearRgb(samples,
                                                    pixel * 3,
                                                    sampleScale,
                                                    highlights,
                                                    shadows,
                                                    whites,
                                                    blacks,
                                                    contrast);
        ++histogram[static_cast<std::size_t>(histogramBinForLuminance(
            linearLuminance(rgb)))];
    }

    const std::uint64_t targetCount = static_cast<std::uint64_t>(std::ceil(
        static_cast<double>(pixelCount) * kDehazeAirlightPercentile));
    std::uint64_t cumulative = 0;
    int thresholdBin = kDehazeAirlightHistogramBins - 1;
    for (int bin = 0; bin < kDehazeAirlightHistogramBins; ++bin) {
        cumulative += histogram[static_cast<std::size_t>(bin)];
        if (cumulative >= targetCount) {
            thresholdBin = bin;
            break;
        }
    }

    const double thresholdEv = kDehazeAirlightMinimumEv
        + histogramEvRange
            * (static_cast<double>(thresholdBin)
               / static_cast<double>(kDehazeAirlightHistogramBins));
    const double thresholdLuminance = std::max(
        minimumLuminance,
        kSceneMiddleGray * std::exp2(thresholdEv));

    LinearRgb weightedSum;
    double totalWeight = 0.0;
    for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
        const LinearRgb rgb = toneAdjustedLinearRgb(samples,
                                                    pixel * 3,
                                                    sampleScale,
                                                    highlights,
                                                    shadows,
                                                    whites,
                                                    blacks,
                                                    contrast);
        const double luminance = linearLuminance(rgb);
        if (luminance < thresholdLuminance) {
            continue;
        }

        const double maximumChannel = std::max({rgb.red, rgb.green, rgb.blue});
        const double minimumChannel = std::min({rgb.red, rgb.green, rgb.blue});
        const double relativeChroma = maximumChannel > minimumLuminance
            ? (maximumChannel - minimumChannel) / maximumChannel
            : 0.0;
        const double neutralityWeight = 1.0
            / (1.0 + kDehazeAirlightNeutralityStrength * relativeChroma);
        const double brightnessWeight = std::sqrt(std::clamp(
            luminance / thresholdLuminance, 1.0, 4.0));
        const double weight = neutralityWeight * brightnessWeight;

        weightedSum.red += rgb.red * weight;
        weightedSum.green += rgb.green * weight;
        weightedSum.blue += rgb.blue * weight;
        totalWeight += weight;
    }

    LinearRgb airlight;
    if (totalWeight > std::numeric_limits<double>::epsilon()) {
        airlight.red = weightedSum.red / totalWeight;
        airlight.green = weightedSum.green / totalWeight;
        airlight.blue = weightedSum.blue / totalWeight;
    } else {
        airlight = {thresholdLuminance, thresholdLuminance, thresholdLuminance};
    }

    double airlightLuminance = linearLuminance(airlight);
    if (!std::isfinite(airlightLuminance)
        || airlightLuminance <= std::numeric_limits<double>::epsilon()) {
        airlight = {1.0, 1.0, 1.0};
        airlightLuminance = 1.0;
    }

    const double channelFloor = std::max(
        minimumLuminance,
        airlightLuminance * kDehazeAirlightChannelFloor);
    const double channelCeiling = std::max(
        channelFloor,
        airlightLuminance * kDehazeAirlightChannelCeiling);
    airlight.red = std::clamp(airlight.red, channelFloor, channelCeiling);
    airlight.green = std::clamp(airlight.green, channelFloor, channelCeiling);
    airlight.blue = std::clamp(airlight.blue, channelFloor, channelCeiling);
    return airlight;
}

double dehazeToneWeight(double luminance)
{
    if (luminance <= std::numeric_limits<double>::epsilon()) {
        return 0.0;
    }

    const double sceneEv = std::log2(luminance / kSceneMiddleGray);
    const double shadowWeight = smoothstep01(
        (sceneEv - kDehazeShadowProtectionStartEv)
        / (kDehazeShadowProtectionFullEv - kDehazeShadowProtectionStartEv));
    const double highlightWeight = 1.0 - smoothstep01(
        (sceneEv - kDehazeHighlightProtectionStartEv)
        / (kDehazeHighlightProtectionEndEv - kDehazeHighlightProtectionStartEv));
    return shadowWeight * highlightWeight;
}

void applyLinearDehaze(double &red,
                       double &green,
                       double &blue,
                       int dehaze,
                       const LinearRgb &airlight)
{
    const int clampedDehaze = std::clamp(dehaze,
                                          kDehazeMinimum,
                                          kDehazeMaximum);
    if (clampedDehaze == 0) {
        return;
    }

    const double amount = std::abs(static_cast<double>(clampedDehaze)) / 100.0;

    if (clampedDehaze < 0) {
        // Negative Dehaze deliberately adds a physically motivated veiling-light
        // layer: attenuate the scene and blend it toward the estimated atmospheric
        // light. This lowers contrast and chroma together instead of applying blur.
        const double veil = kDehazeNegativeMaximumVeil * amount;
        const double transmission = 1.0 - veil;
        red = red * transmission + airlight.red * veil;
        green = green * transmission + airlight.green * veil;
        blue = blue * transmission + airlight.blue * veil;
        return;
    }

    const LinearRgb rgb{red, green, blue};
    const double luminance = linearLuminance(rgb);
    const double toneWeight = dehazeToneWeight(luminance);
    if (toneWeight <= 0.0) {
        return;
    }

    constexpr double minimumAirlight = 1e-8;
    const double normalizedMinimum = std::min({
        red / std::max(airlight.red, minimumAirlight),
        green / std::max(airlight.green, minimumAirlight),
        blue / std::max(airlight.blue, minimumAirlight),
    });
    const double hazeEvidence = smoothstep01(normalizedMinimum);
    if (hazeEvidence <= 0.0) {
        return;
    }

    const double removal = kDehazePositiveMaximumRemoval * amount * toneWeight;
    const double transmission = std::clamp(
        1.0 - removal * hazeEvidence,
        kDehazeMinimumTransmission,
        1.0);
    const double veil = 1.0 - transmission;

    // Invert the atmospheric-scattering equation I = J*t + A*(1-t). The
    // min-channel haze estimate guarantees the subtraction remains non-negative
    // for removal <= 1, while highlight/shadow protection keeps extremes stable.
    red = std::max(0.0, (red - airlight.red * veil) / transmission);
    green = std::max(0.0, (green - airlight.green * veil) / transmission);
    blue = std::max(0.0, (blue - airlight.blue * veil) / transmission);
}

Oklab linearSrgbToOklab(const LinearRgb &rgb)
{
    const double l = 0.4122214708 * rgb.red
        + 0.5363325363 * rgb.green
        + 0.0514459929 * rgb.blue;
    const double m = 0.2119034982 * rgb.red
        + 0.6806995451 * rgb.green
        + 0.1073969566 * rgb.blue;
    const double s = 0.0883024619 * rgb.red
        + 0.2817188376 * rgb.green
        + 0.6299787005 * rgb.blue;

    const double lRoot = std::cbrt(l);
    const double mRoot = std::cbrt(m);
    const double sRoot = std::cbrt(s);

    return {
        0.2104542553 * lRoot + 0.7936177850 * mRoot - 0.0040720468 * sRoot,
        1.9779984951 * lRoot - 2.4285922050 * mRoot + 0.4505937099 * sRoot,
        0.0259040371 * lRoot + 0.7827717662 * mRoot - 0.8086757660 * sRoot,
    };
}

LinearRgb oklabToLinearSrgb(const Oklab &lab)
{
    const double lRoot = lab.lightness + 0.3963377774 * lab.a + 0.2158037573 * lab.b;
    const double mRoot = lab.lightness - 0.1055613458 * lab.a - 0.0638541728 * lab.b;
    const double sRoot = lab.lightness - 0.0894841775 * lab.a - 1.2914855480 * lab.b;

    const double l = lRoot * lRoot * lRoot;
    const double m = mRoot * mRoot * mRoot;
    const double s = sRoot * sRoot * sRoot;

    return {
        4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
        -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
        -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s,
    };
}

Oklab oklabWithChroma(const Oklab &lab, double chroma)
{
    const double currentChroma = std::hypot(lab.a, lab.b);
    if (currentChroma <= std::numeric_limits<double>::epsilon()) {
        return lab;
    }

    const double scale = chroma / currentChroma;
    return {lab.lightness, lab.a * scale, lab.b * scale};
}

bool isUnitGamut(const LinearRgb &rgb)
{
    constexpr double epsilon = 1e-7;
    return rgb.red >= -epsilon && rgb.red <= 1.0 + epsilon
        && rgb.green >= -epsilon && rgb.green <= 1.0 + epsilon
        && rgb.blue >= -epsilon && rgb.blue <= 1.0 + epsilon;
}

double warmHueVibranceScale(const Oklab &lab, double chroma)
{
    if (chroma <= 0.02) {
        return 1.0;
    }

    const double hue = std::atan2(lab.b, lab.a);
    const double hueDistance = std::abs(std::remainder(
        hue - kWarmHueCenterRadians, kTwoPi));
    const double hueWeight = 1.0 - smoothstep01(
        hueDistance / kWarmHueHalfWidthRadians);

    const double lowLightnessWeight = smoothstep01(
        (lab.lightness - 0.20) / 0.15);
    const double highLightnessWeight = smoothstep01(
        (0.95 - lab.lightness) / 0.15);
    const double chromaWeight = smoothstep01((chroma - 0.02) / 0.06);
    const double protection = hueWeight
        * lowLightnessWeight
        * highLightnessWeight
        * chromaWeight;

    return 1.0 - kWarmHueProtectionStrength * protection;
}

void applyOklabVibrance(double &red,
                        double &green,
                        double &blue,
                        int vibrance)
{
    const int clampedVibrance = std::clamp(vibrance,
                                           kVibranceMinimum,
                                           kVibranceMaximum);
    if (clampedVibrance == 0) {
        return;
    }

    // Normalize only for the perceptual color calculation so Exposure remains a
    // separate linear-light control and Vibrance does not become stronger simply
    // because a pixel is above display white.
    const double normalization = std::max({1.0, red, green, blue});
    const LinearRgb normalizedRgb{
        red / normalization,
        green / normalization,
        blue / normalization,
    };

    const Oklab lab = linearSrgbToOklab(normalizedRgb);
    const double chroma = std::hypot(lab.a, lab.b);
    if (chroma <= 1e-10) {
        return;
    }

    const double normalizedChroma = std::clamp(
        chroma / kOklabReferenceChroma, 0.0, 1.0);
    const double amount = std::abs(static_cast<double>(clampedVibrance)) / 100.0;

    double chromaMultiplier = 1.0;
    if (clampedVibrance > 0) {
        const double mutedWeight = (1.0 - normalizedChroma)
            * (1.0 - normalizedChroma);
        chromaMultiplier += amount
            * mutedWeight
            * warmHueVibranceScale(lab, chroma);
    } else {
        const double colorfulWeight = 0.35 + 0.65 * normalizedChroma;
        chromaMultiplier = std::max(0.0, 1.0 - amount * colorfulWeight);
    }

    const double targetChroma = chroma * chromaMultiplier;
    LinearRgb adjusted = oklabToLinearSrgb(oklabWithChroma(lab, targetChroma));

    // Positive Vibrance should not create new channel clipping. If the requested
    // Oklab chroma falls outside normalized linear sRGB, back off chroma while
    // preserving Oklab lightness and hue.
    if (clampedVibrance > 0 && targetChroma > chroma && !isUnitGamut(adjusted)) {
        double lowerChroma = chroma;
        double upperChroma = targetChroma;
        adjusted = normalizedRgb;

        for (int iteration = 0; iteration < kVibranceGamutSearchIterations; ++iteration) {
            const double candidateChroma = 0.5 * (lowerChroma + upperChroma);
            const LinearRgb candidate = oklabToLinearSrgb(
                oklabWithChroma(lab, candidateChroma));

            if (isUnitGamut(candidate)) {
                lowerChroma = candidateChroma;
                adjusted = candidate;
            } else {
                upperChroma = candidateChroma;
            }
        }
    }

    red = std::max(0.0, adjusted.red * normalization);
    green = std::max(0.0, adjusted.green * normalization);
    blue = std::max(0.0, adjusted.blue * normalization);
}

QImage developedSrgbImage(const libraw_processed_image_t &processed,
                          int exposureHundredths,
                          int highlights,
                          int shadows,
                          int whites,
                          int blacks,
                          int contrast,
                          int texture,
                          int clarity,
                          int dehaze,
                          int saturation,
                          int vibrance,
                          QString *error)
{
    if (processed.type != LIBRAW_IMAGE_BITMAP
        || processed.width == 0
        || processed.height == 0
        || processed.colors != 3
        || processed.bits != 16) {
        setError(error, QStringLiteral("LibRaw returned an unsupported linear ARW bitmap format."));
        return {};
    }

    const std::size_t pixelCount =
        static_cast<std::size_t>(processed.width) * processed.height;
    const std::size_t sampleCount = pixelCount * 3;
    const std::size_t requiredBytes = sampleCount * sizeof(std::uint16_t);
    if (processed.data_size < requiredBytes) {
        setError(error, QStringLiteral("LibRaw returned an incomplete linear ARW bitmap."));
        return {};
    }

    const auto *samples = reinterpret_cast<const std::uint16_t *>(processed.data);
    const int whitePoint = autoBrightnessWhitePoint(samples, pixelCount);
    const double baselineGain = static_cast<double>(kLinearMaximum)
        / static_cast<double>(whitePoint);
    const int clampedExposure = std::clamp(exposureHundredths,
                                           kExposureMinimumHundredths,
                                           kExposureMaximumHundredths);
    const double exposureGain = std::exp2(static_cast<double>(clampedExposure) / 100.0);
    const double totalGain = baselineGain * exposureGain;
    const int clampedHighlights = std::clamp(highlights,
                                              kHighlightsMinimum,
                                              kHighlightsMaximum);
    const int clampedShadows = std::clamp(shadows,
                                           kShadowsMinimum,
                                           kShadowsMaximum);
    const int clampedWhites = std::clamp(whites,
                                         kWhitesMinimum,
                                         kWhitesMaximum);
    const int clampedBlacks = std::clamp(blacks,
                                         kBlacksMinimum,
                                         kBlacksMaximum);
    const int clampedContrast = std::clamp(contrast,
                                           kContrastMinimum,
                                           kContrastMaximum);
    const int clampedTexture = std::clamp(texture,
                                          kTextureMinimum,
                                          kTextureMaximum);
    const int clampedClarity = std::clamp(clarity,
                                          kClarityMinimum,
                                          kClarityMaximum);
    const int clampedDehaze = std::clamp(dehaze,
                                         kDehazeMinimum,
                                         kDehazeMaximum);
    const int clampedSaturation = std::clamp(saturation,
                                             kSaturationMinimum,
                                             kSaturationMaximum);
    const int clampedVibrance = std::clamp(vibrance,
                                           kVibranceMinimum,
                                           kVibranceMaximum);

    QImage image(processed.width, processed.height, QImage::Format_RGB888);
    if (image.isNull()) {
        setError(error, QStringLiteral("Not enough memory to create the ARW preview."));
        return {};
    }

    if (clampedHighlights == 0 && clampedShadows == 0 && clampedWhites == 0
        && clampedBlacks == 0 && clampedContrast == 0 && clampedTexture == 0
        && clampedClarity == 0 && clampedDehaze == 0
        && clampedSaturation == 0 && clampedVibrance == 0) {
        // Keep the neutral path byte-for-byte equivalent to the existing exposure
        // render and avoid per-pixel floating-point work when develop controls are 0.
        std::vector<std::uint8_t> transferTable(kLinearMaximum + 1);
        for (int value = 0; value <= kLinearMaximum; ++value) {
            const double linear = static_cast<double>(value)
                / static_cast<double>(kLinearMaximum);
            transferTable[static_cast<std::size_t>(value)] = linearToSrgb8(linear * totalGain);
        }

        for (int y = 0; y < processed.height; ++y) {
            auto *destination = image.scanLine(y);
            const std::size_t sourceRow = static_cast<std::size_t>(y) * processed.width * 3;
            for (int x = 0; x < processed.width; ++x) {
                const std::size_t source = sourceRow + static_cast<std::size_t>(x) * 3;
                const int destinationOffset = x * 3;
                destination[destinationOffset] = transferTable[samples[source]];
                destination[destinationOffset + 1] = transferTable[samples[source + 1]];
                destination[destinationOffset + 2] = transferTable[samples[source + 2]];
            }
        }
    } else {
        const double chromaScale = saturationChromaScale(clampedSaturation);
        const double sampleScale = totalGain / static_cast<double>(kLinearMaximum);
        std::vector<float> textureBand;
        std::vector<float> textureScratch;
        std::vector<float> clarityBand;
        std::vector<float> clarityScratch;

        if (clampedTexture != 0
            && !buildTextureBand(samples,
                                 pixelCount,
                                 static_cast<int>(processed.width),
                                 static_cast<int>(processed.height),
                                 sampleScale,
                                 clampedHighlights,
                                 clampedShadows,
                                 clampedWhites,
                                 clampedBlacks,
                                 clampedContrast,
                                 textureBand,
                                 textureScratch,
                                 error)) {
            return {};
        }

        if (clampedClarity != 0
            && !buildClarityBand(samples,
                                 pixelCount,
                                 static_cast<int>(processed.width),
                                 static_cast<int>(processed.height),
                                 sampleScale,
                                 clampedHighlights,
                                 clampedShadows,
                                 clampedWhites,
                                 clampedBlacks,
                                 clampedContrast,
                                 clarityBand,
                                 clarityScratch,
                                 error)) {
            return {};
        }

        LinearRgb dehazeAirlight{1.0, 1.0, 1.0};
        if (clampedDehaze != 0) {
            dehazeAirlight = estimateDehazeAirlight(samples,
                                                    pixelCount,
                                                    sampleScale,
                                                    clampedHighlights,
                                                    clampedShadows,
                                                    clampedWhites,
                                                    clampedBlacks,
                                                    clampedContrast);
        }

        for (int y = 0; y < processed.height; ++y) {
            auto *destination = image.scanLine(y);
            const std::size_t sourceRow = static_cast<std::size_t>(y) * processed.width * 3;
            for (int x = 0; x < processed.width; ++x) {
                const std::size_t pixel = static_cast<std::size_t>(y) * processed.width + x;
                const std::size_t source = sourceRow + static_cast<std::size_t>(x) * 3;
                const int destinationOffset = x * 3;

                LinearRgb rgb = toneAdjustedLinearRgb(samples,
                                                       source,
                                                       sampleScale,
                                                       clampedHighlights,
                                                       clampedShadows,
                                                       clampedWhites,
                                                       clampedBlacks,
                                                       clampedContrast);

                if (clampedTexture != 0) {
                    applyLinearTexture(rgb.red,
                                       rgb.green,
                                       rgb.blue,
                                       clampedTexture,
                                       textureBand[pixel]);
                }
                if (clampedClarity != 0) {
                    applyLinearClarity(rgb.red,
                                       rgb.green,
                                       rgb.blue,
                                       clampedClarity,
                                       clarityBand[pixel]);
                }
                if (clampedDehaze != 0) {
                    applyLinearDehaze(rgb.red,
                                      rgb.green,
                                      rgb.blue,
                                      clampedDehaze,
                                      dehazeAirlight);
                }
                if (clampedVibrance != 0) {
                    applyOklabVibrance(rgb.red, rgb.green, rgb.blue, clampedVibrance);
                }
                if (clampedSaturation != 0) {
                    applyLinearSaturation(rgb.red, rgb.green, rgb.blue, chromaScale);
                }

                destination[destinationOffset] = linearToSrgb8(rgb.red);
                destination[destinationOffset + 1] = linearToSrgb8(rgb.green);
                destination[destinationOffset + 2] = linearToSrgb8(rgb.blue);
            }
        }
    }

    image.setDevicePixelRatio(1.0);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace

QImage ArwDecoder::decode(const QString &filePath,
                          QString *error,
                          PhotoMetadata *metadata,
                          int whiteBalanceTemperatureKelvin,
                          int tint,
                          int exposureHundredths,
                          int highlights,
                          int shadows,
                          int whites,
                          int blacks,
                          int contrast,
                          int texture,
                          int clarity,
                          int dehaze,
                          int saturation,
                          int vibrance,
                          const RawCalibration &calibration,
                          bool halfSizePreview)
{
    if (error != nullptr) {
        error->clear();
    }
    if (metadata != nullptr) {
        *metadata = {};
    }

    CalibratedLibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    processor.setRawCalibration(calibration);

    // Ask LibRaw for a high-precision linear sRGB render. Apollo's Calibration
    // is applied inside LibRaw's camera-to-RGB conversion; the remaining develop
    // controls and final sRGB transfer operate on that calibrated linear result.
    processor.imgdata.params.use_camera_wb = 1;
    processor.imgdata.params.use_camera_matrix = 1;
    processor.imgdata.params.output_color = 1; // linear sRGB primaries
    processor.imgdata.params.output_bps = 16;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.adjust_maximum_thr = 0.0f;
    processor.imgdata.params.half_size = halfSizePreview ? 1 : 0;
    processor.imgdata.params.gamm[0] = 1.0;
    processor.imgdata.params.gamm[1] = 1.0;

    int result = openFile(processor, filePath);
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Opening ARW", result));
        return {};
    }

    const std::vector<TemperatureCalibration> calibrations =
        sonyTemperatureCalibrations(processor.imgdata.color);
    const int cameraTemperatureKelvin =
        sonyWhiteBalanceTemperatureKelvin(processor.imgdata.color, calibrations);

    if (metadata != nullptr) {
        const libraw_imgother_t &other = processor.imgdata.other;
        metadata->shutterSeconds = other.shutter;
        metadata->aperture = other.aperture;
        metadata->iso = other.iso_speed;
        metadata->focalLengthMm = other.focal_len;
        metadata->whiteBalanceTemperatureKelvin = cameraTemperatureKelvin;
    }

    const int requestedTemperatureKelvin = whiteBalanceTemperatureKelvin > 0
        ? std::clamp(whiteBalanceTemperatureKelvin,
                     kSonyTemperatureMinimumKelvin,
                     kSonyTemperatureMaximumKelvin)
        : 0;
    const int requestedTint = std::clamp(tint, kTintMinimum, kTintMaximum);

    if (requestedTemperatureKelvin > 0
        && (requestedTemperatureKelvin != cameraTemperatureKelvin || requestedTint != 0)) {
        float userMultipliers[4] = {};
        if (cameraTemperatureKelvin <= 0
            || !sonyWhiteBalanceMultipliersForDevelopState(processor.imgdata.color,
                                                           calibrations,
                                                           cameraTemperatureKelvin,
                                                           requestedTemperatureKelvin,
                                                           requestedTint,
                                                           userMultipliers)) {
            setError(error,
                     QStringLiteral("Sony white-balance calibration data is unavailable for this RAW file."));
            return {};
        }

        processor.imgdata.params.use_camera_wb = 0;
        for (int channel = 0; channel < 4; ++channel) {
            processor.imgdata.params.user_mul[channel] = userMultipliers[channel];
        }
    }

    result = processor.unpack();
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Decoding ARW", result));
        return {};
    }

    result = processor.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Rendering ARW", result));
        return {};
    }

    int memoryResult = LIBRAW_SUCCESS;
    libraw_processed_image_t *processed = processor.dcraw_make_mem_image(&memoryResult);
    if (processed == nullptr) {
        setError(error, memoryResult == LIBRAW_SUCCESS
            ? QStringLiteral("LibRaw could not create a linear ARW bitmap.")
            : libRawError("Copying ARW pixels", memoryResult));
        return {};
    }

    // dcraw_make_mem_image() owns its returned buffer independently of the
    // LibRaw instance, so release the decoded RAW and postprocessed buffers before
    // allocating Apollo's final display image.
    processor.recycle();

    QImage image = developedSrgbImage(*processed,
                                      exposureHundredths,
                                      highlights,
                                      shadows,
                                      whites,
                                      blacks,
                                      contrast,
                                      texture,
                                      clarity,
                                      dehaze,
                                      saturation,
                                      vibrance,
                                      error);
    LibRaw::dcraw_clear_mem(processed);
    return image;
}

WhiteBalancePickResult ArwDecoder::pickWhiteBalance(
    const QString &filePath,
    const QPointF &normalizedPosition)
{
    WhiteBalancePickResult pick;

    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    processor.imgdata.params.use_camera_wb = 0;
    processor.imgdata.params.use_auto_wb = 1;
    processor.imgdata.params.use_camera_matrix = 1;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.no_interpolation = 1;

    int result = openFile(processor, filePath);
    if (result != LIBRAW_SUCCESS) {
        pick.error = libRawError("Opening ARW for white-balance sampling", result);
        return pick;
    }

    const std::vector<TemperatureCalibration> calibrations =
        sonyTemperatureCalibrations(processor.imgdata.color);
    const int cameraTemperatureKelvin =
        sonyWhiteBalanceTemperatureKelvin(processor.imgdata.color, calibrations);
    if (cameraTemperatureKelvin <= 0 || calibrations.size() < 2) {
        pick.error = QStringLiteral(
            "Sony white-balance calibration data is unavailable for this RAW file.");
        return pick;
    }

    if (!configureGreyBox(processor, normalizedPosition)) {
        pick.error = QStringLiteral("The selected white-balance sample is outside the image.");
        return pick;
    }

    result = processor.unpack();
    if (result != LIBRAW_SUCCESS) {
        pick.error = libRawError("Decoding ARW for white-balance sampling", result);
        return pick;
    }

    result = processor.dcraw_process();
    if (result != LIBRAW_SUCCESS) {
        pick.error = libRawError("Sampling RAW white balance", result);
        return pick;
    }

    if (!sonyWhiteBalanceValuesForMultipliers(processor.imgdata.color,
                                              calibrations,
                                              cameraTemperatureKelvin,
                                              processor.imgdata.color.pre_mul,
                                              pick.temperatureKelvin,
                                              pick.tint)) {
        pick.error = QStringLiteral(
            "The selected area did not provide a usable neutral white-balance sample.");
        pick.temperatureKelvin = 0;
        pick.tint = 0;
    }

    return pick;
}

} // namespace imaging
