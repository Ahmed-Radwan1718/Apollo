#include "imaging/ArwDecoder.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QColorSpace>
#include <QFile>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace imaging {
namespace {

constexpr int kSonyTemperatureMinimumKelvin = 2000;
constexpr int kSonyTemperatureMaximumKelvin = 50000;
constexpr int kSonyTemperatureStepKelvin = 100;

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
        if (kelvin < 2000 || kelvin > 20000) {
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

    // Match the camera-recorded WB multipliers against Sony's calibration table
    // in reciprocal-temperature space. Nexus keeps a broad editor range rather
    // than restricting Temp to the controls offered by any single camera body.
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

} // namespace

QImage ArwDecoder::decode(const QString &filePath,
                          QString *error,
                          PhotoMetadata *metadata,
                          int whiteBalanceTemperatureKelvin,
                          bool halfSizePreview)
{
    if (error != nullptr) {
        error->clear();
    }
    if (metadata != nullptr) {
        *metadata = {};
    }

    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);

    // Produce a display-ready rendering from the RAW data itself. LibRaw's normal
    // histogram-based auto brightness is intentionally left enabled so 8-bit output
    // uses a practical display range instead of appearing severely underexposed.
    // Interactive Temp previews use LibRaw's half-size path to avoid a full
    // demosaic on every slider position. The original ARW is never modified.
    processor.imgdata.params.use_camera_wb = 1;
    processor.imgdata.params.use_camera_matrix = 1;
    processor.imgdata.params.output_color = 1; // sRGB
    processor.imgdata.params.output_bps = 8;
    processor.imgdata.params.half_size = halfSizePreview ? 1 : 0;
    processor.imgdata.params.gamm[0] = 1.0 / 2.4;
    processor.imgdata.params.gamm[1] = 12.92;

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

        // use_camera_wb is deliberately enabled while opening so LibRaw loads the
        // normal Sony camera-color path. For an actual Temp edit, switch only the
        // post-processing WB source to user_mul; otherwise camera WB would override it.
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

    int width = 0;
    int height = 0;
    int colors = 0;
    int bitsPerColor = 0;
    processor.get_mem_image_format(&width, &height, &colors, &bitsPerColor);

    if (width <= 0 || height <= 0 || colors != 3 || bitsPerColor != 8) {
        setError(error, QStringLiteral("LibRaw returned an unsupported ARW bitmap format."));
        return {};
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        setError(error, QStringLiteral("Not enough memory to create the ARW preview."));
        return {};
    }

    if (image.bytesPerLine() > std::numeric_limits<int>::max()) {
        setError(error, QStringLiteral("The ARW image row is too large to render."));
        return {};
    }

    // Copy directly into QImage using its native row stride. This avoids allocating
    // a second full-resolution RGB buffer for large Sony files.
    result = processor.copy_mem_image(
        image.bits(),
        static_cast<int>(image.bytesPerLine()),
        0); // RGB order

    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Copying ARW pixels", result));
        return {};
    }

    image.setDevicePixelRatio(1.0);
    image.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return image;
}

} // namespace imaging
