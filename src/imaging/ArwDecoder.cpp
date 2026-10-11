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
constexpr int kExposureMinimumHundredths = -500;
constexpr int kExposureMaximumHundredths = 500;
constexpr int kLinearMaximum = 65535;
constexpr double kAutoBrightnessClipFraction = 0.01;

struct WhiteBalanceRatios {
    double redToGreen = 0.0;
    double blueToGreen = 0.0;
};

struct TemperatureCalibration {
    int kelvin = 0;
    WhiteBalanceRatios ratios;
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

bool sonyWhiteBalanceMultipliersForTemperature(
    const libraw_colordata_t &color,
    const std::vector<TemperatureCalibration> &calibrations,
    int cameraKelvin,
    int targetKelvin,
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
    // as-shot offset from that curve. That preserves the camera's green/magenta
    // bias so Temp changes temperature only; Tint remains a separate future edit.
    const double redToGreen = targetCurveRatios.redToGreen
        + (asShotRatios.redToGreen - cameraCurveRatios.redToGreen);
    const double blueToGreen = targetCurveRatios.blueToGreen
        + (asShotRatios.blueToGreen - cameraCurveRatios.blueToGreen);

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

QImage exposedSrgbImage(const libraw_processed_image_t &processed,
                        int exposureHundredths,
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

    std::vector<std::uint8_t> transferTable(kLinearMaximum + 1);
    for (int value = 0; value <= kLinearMaximum; ++value) {
        const double linear = static_cast<double>(value)
            / static_cast<double>(kLinearMaximum);
        transferTable[static_cast<std::size_t>(value)] = linearToSrgb8(linear * totalGain);
    }

    QImage image(processed.width, processed.height, QImage::Format_RGB888);
    if (image.isNull()) {
        setError(error, QStringLiteral("Not enough memory to create the ARW preview."));
        return {};
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

    image.setDevicePixelRatio(1.0);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace

QImage ArwDecoder::decode(const QString &filePath,
                          QString *error,
                          PhotoMetadata *metadata,
                          int whiteBalanceTemperatureKelvin,
                          int exposureHundredths,
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
    // final exposure multiplication and sRGB transfer itself so ±5 EV remains
    // a true 2^EV linear-light adjustment instead of an 8-bit brightness filter.
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

    if (requestedTemperatureKelvin > 0
        && requestedTemperatureKelvin != cameraTemperatureKelvin) {
        float userMultipliers[4] = {};
        if (cameraTemperatureKelvin <= 0
            || !sonyWhiteBalanceMultipliersForTemperature(processor.imgdata.color,
                                                          calibrations,
                                                          cameraTemperatureKelvin,
                                                          requestedTemperatureKelvin,
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

    QImage image = exposedSrgbImage(*processed, exposureHundredths, error);
    LibRaw::dcraw_clear_mem(processed);
    return image;
}

} // namespace imaging
