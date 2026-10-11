#include "imaging/ArwDecoder.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QColorSpace>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
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

    if (clampedSaturation == 0 && clampedVibrance == 0) {
        // Keep the neutral path byte-for-byte equivalent to the existing exposure
        // render and avoid per-pixel floating-point color work when color controls are 0.
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

        for (int y = 0; y < processed.height; ++y) {
            auto *destination = image.scanLine(y);
            const std::size_t sourceRow = static_cast<std::size_t>(y) * processed.width * 3;
            for (int x = 0; x < processed.width; ++x) {
                const std::size_t source = sourceRow + static_cast<std::size_t>(x) * 3;
                const int destinationOffset = x * 3;

                double red = static_cast<double>(samples[source]) * sampleScale;
                double green = static_cast<double>(samples[source + 1]) * sampleScale;
                double blue = static_cast<double>(samples[source + 2]) * sampleScale;

                if (clampedVibrance != 0) {
                    applyOklabVibrance(red, green, blue, clampedVibrance);
                }
                if (clampedSaturation != 0) {
                    applyLinearSaturation(red, green, blue, chromaScale);
                }

                destination[destinationOffset] = linearToSrgb8(red);
                destination[destinationOffset + 1] = linearToSrgb8(green);
                destination[destinationOffset + 2] = linearToSrgb8(blue);
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
                          int saturation,
                          int vibrance,
                          bool halfSizePreview)
{
    if (error != nullptr) {
        error->clear();
    }
    if (metadata != nullptr) {
        *metadata = {};
    }

    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);

    // Ask LibRaw for a high-precision linear sRGB render. Nexus performs the
    // final exposure, perceptual vibrance, saturation, and sRGB transfer itself
    // instead of applying color changes to an already rendered 8-bit image.
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

    QImage image = developedSrgbImage(*processed,
                                      exposureHundredths,
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
