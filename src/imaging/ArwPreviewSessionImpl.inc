#include "imaging/ArwPreviewSession.h"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstring>
#include <memory>
#include <numbers>

namespace imaging {

struct ArwPreviewSession::Impl
{
    static constexpr int kMaximumPreviewLongSide = 1280;
    static constexpr double kSceneMiddleGray = 0.18;
    static constexpr double kShadowTintStartEv = -0.5;
    static constexpr double kShadowTintFullEv = -3.0;
    static constexpr double kShadowTintMaximumLogShift = 0.30010459245033816; // ln(1.35)
    static constexpr double kMaximumPrimaryHueDegrees = 30.0;
    static constexpr double kLinearSrgbRedLuminance = 0.2126;
    static constexpr double kLinearSrgbGreenLuminance = 0.7152;
    static constexpr double kLinearSrgbBlueLuminance = 0.0722;
    using Matrix3 = std::array<double, 9>;

    ~Impl()
    {
        clearCachedImage();
    }

    static int clampedCalibrationValue(int value)
    {
        return std::clamp(value, -100, 100);
    }

    static double smoothstep01(double value)
    {
        const double clamped = std::clamp(value, 0.0, 1.0);
        return clamped * clamped * (3.0 - 2.0 * clamped);
    }

    static double shadowTintWeight(double linearLuminance)
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

    static void adjustPrimaryColumn(Matrix3 &matrix,
                                    int column,
                                    int hue,
                                    int saturation)
    {
        const int clampedHue = clampedCalibrationValue(hue);
        const int clampedSaturation = clampedCalibrationValue(saturation);
        if (clampedHue == 0 && clampedSaturation == 0) {
            return;
        }

        const double red = matrix[column];
        const double green = matrix[3 + column];
        const double blue = matrix[6 + column];
        const double luminance = red * kLinearSrgbRedLuminance
            + green * kLinearSrgbGreenLuminance
            + blue * kLinearSrgbBlueLuminance;

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
            matrix[column] = calibratedRed;
            matrix[3 + column] = calibratedGreen;
            matrix[6 + column] = calibratedBlue;
        }
    }

    static void destroyLinearImage(libraw_processed_image_t *&image)
    {
        if (image != nullptr) {
            std::destroy_at(image);
            ::operator delete(image);
            image = nullptr;
        }
    }

    void clearCachedImage()
    {
        destroyLinearImage(cachedImage);
        destroyLinearImage(temperaturePreviewImage);
        cachedTemperatureKelvin = std::numeric_limits<int>::min();
        cachedTint = std::numeric_limits<int>::min();
        cachedCalibration = {};
    }

    void reset()
    {
        clearCachedImage();
        processor.reset();
        filePath.clear();
        calibrations.clear();
        cameraTemperatureKelvin = 0;
    }

    bool ensureRaw(const QString &requestedFilePath, QString *error)
    {
        if (processor && filePath == requestedFilePath) {
            return true;
        }

        reset();

        auto candidate = std::make_unique<CalibratedLibRaw>(
            LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
        candidate->imgdata.params.use_camera_wb = 1;
        candidate->imgdata.params.use_camera_matrix = 1;
        candidate->imgdata.params.output_color = 1; // linear sRGB primaries
        candidate->imgdata.params.output_bps = 16;
        candidate->imgdata.params.no_auto_bright = 1;
        candidate->imgdata.params.adjust_maximum_thr = 0.0f;
        candidate->imgdata.params.half_size = 1;
        candidate->imgdata.params.gamm[0] = 1.0;
        candidate->imgdata.params.gamm[1] = 1.0;

        int result = openFile(*candidate, requestedFilePath);
        if (result != LIBRAW_SUCCESS) {
            setError(error, libRawError("Opening ARW preview", result));
            return false;
        }

        std::vector<TemperatureCalibration> candidateCalibrations =
            sonyTemperatureCalibrations(candidate->imgdata.color);
        const int candidateCameraTemperatureKelvin =
            sonyWhiteBalanceTemperatureKelvin(candidate->imgdata.color,
                                               candidateCalibrations);

        result = candidate->unpack();
        if (result != LIBRAW_SUCCESS) {
            setError(error, libRawError("Decoding ARW preview", result));
            return false;
        }

        filePath = requestedFilePath;
        calibrations = std::move(candidateCalibrations);
        cameraTemperatureKelvin = candidateCameraTemperatureKelvin;
        processor = std::move(candidate);
        return true;
    }

    bool cacheLinearPreview(const libraw_processed_image_t &processed,
                            QString *error)
    {
        if (processed.type != LIBRAW_IMAGE_BITMAP
            || processed.width == 0
            || processed.height == 0
            || processed.colors != 3
            || processed.bits != 16) {
            setError(error,
                     QStringLiteral("LibRaw returned an unsupported linear ARW preview format."));
            return false;
        }

        const int sourceWidth = static_cast<int>(processed.width);
        const int sourceHeight = static_cast<int>(processed.height);
        const std::size_t sourcePixelCount =
            static_cast<std::size_t>(sourceWidth) * sourceHeight;
        const std::size_t sourceBytes = sourcePixelCount * 3 * sizeof(std::uint16_t);
        if (processed.data_size < sourceBytes) {
            setError(error,
                     QStringLiteral("LibRaw returned an incomplete linear ARW preview."));
            return false;
        }

        const int sourceLongSide = std::max(sourceWidth, sourceHeight);
        const double scale = sourceLongSide > kMaximumPreviewLongSide
            ? static_cast<double>(kMaximumPreviewLongSide)
                / static_cast<double>(sourceLongSide)
            : 1.0;
        const int targetWidth = std::max(1,
            static_cast<int>(std::lround(sourceWidth * scale)));
        const int targetHeight = std::max(1,
            static_cast<int>(std::lround(sourceHeight * scale)));
        const std::size_t targetPixelCount =
            static_cast<std::size_t>(targetWidth) * targetHeight;
        const std::size_t targetBytes = targetPixelCount * 3 * sizeof(std::uint16_t);

        if (targetBytes > std::numeric_limits<unsigned int>::max()) {
            setError(error,
                     QStringLiteral("The ARW preview buffer is too large."));
            return false;
        }

        const std::size_t allocationBytes =
            offsetof(libraw_processed_image_t, data) + targetBytes;
        void *storage = ::operator new(allocationBytes, std::nothrow);
        if (storage == nullptr) {
            setError(error,
                     QStringLiteral("Not enough memory to cache the ARW preview."));
            return false;
        }

        auto *candidate = ::new (storage) libraw_processed_image_t{};
        candidate->type = LIBRAW_IMAGE_BITMAP;
        candidate->width = static_cast<unsigned short>(targetWidth);
        candidate->height = static_cast<unsigned short>(targetHeight);
        candidate->colors = 3;
        candidate->bits = 16;
        candidate->data_size = static_cast<unsigned int>(targetBytes);

        const auto *source = reinterpret_cast<const std::uint16_t *>(processed.data);
        auto *destination = reinterpret_cast<std::uint16_t *>(candidate->data);

        if (targetWidth == sourceWidth && targetHeight == sourceHeight) {
            std::memcpy(destination, source, targetBytes);
        } else {
            const double xScale = static_cast<double>(sourceWidth)
                / static_cast<double>(targetWidth);
            const double yScale = static_cast<double>(sourceHeight)
                / static_cast<double>(targetHeight);

            for (int y = 0; y < targetHeight; ++y) {
                const double sourceY = std::clamp(
                    (static_cast<double>(y) + 0.5) * yScale - 0.5,
                    0.0,
                    static_cast<double>(sourceHeight - 1));
                const int y0 = static_cast<int>(std::floor(sourceY));
                const int y1 = std::min(y0 + 1, sourceHeight - 1);
                const double yBlend = sourceY - static_cast<double>(y0);

                for (int x = 0; x < targetWidth; ++x) {
                    const double sourceX = std::clamp(
                        (static_cast<double>(x) + 0.5) * xScale - 0.5,
                        0.0,
                        static_cast<double>(sourceWidth - 1));
                    const int x0 = static_cast<int>(std::floor(sourceX));
                    const int x1 = std::min(x0 + 1, sourceWidth - 1);
                    const double xBlend = sourceX - static_cast<double>(x0);

                    const std::size_t topLeft =
                        (static_cast<std::size_t>(y0) * sourceWidth + x0) * 3;
                    const std::size_t topRight =
                        (static_cast<std::size_t>(y0) * sourceWidth + x1) * 3;
                    const std::size_t bottomLeft =
                        (static_cast<std::size_t>(y1) * sourceWidth + x0) * 3;
                    const std::size_t bottomRight =
                        (static_cast<std::size_t>(y1) * sourceWidth + x1) * 3;
                    const std::size_t target =
                        (static_cast<std::size_t>(y) * targetWidth + x) * 3;

                    for (int channel = 0; channel < 3; ++channel) {
                        const double top = source[topLeft + channel]
                            + xBlend * (static_cast<double>(source[topRight + channel])
                                        - source[topLeft + channel]);
                        const double bottom = source[bottomLeft + channel]
                            + xBlend * (static_cast<double>(source[bottomRight + channel])
                                        - source[bottomLeft + channel]);
                        const double value = top + yBlend * (bottom - top);
                        destination[target + channel] = static_cast<std::uint16_t>(
                            std::clamp(static_cast<int>(std::lround(value)),
                                       0,
                                       kLinearMaximum));
                    }
                }
            }
        }

        clearCachedImage();
        cachedImage = candidate;
        return true;
    }

    bool refreshWhiteBalance(int whiteBalanceTemperatureKelvin,
                             int tint,
                             const RawCalibration &calibration,
                             QString *error)
    {
        const int requestedTemperatureKelvin = whiteBalanceTemperatureKelvin > 0
            ? std::clamp(whiteBalanceTemperatureKelvin,
                         kSonyTemperatureMinimumKelvin,
                         kSonyTemperatureMaximumKelvin)
            : 0;
        const int requestedTint = std::clamp(tint, kTintMinimum, kTintMaximum);

        if (cachedImage != nullptr
            && cachedTemperatureKelvin == requestedTemperatureKelvin
            && cachedTint == requestedTint
            && cachedCalibration == calibration) {
            return true;
        }

        processor->free_image();
        processor->setRawCalibration(calibration);
        processor->imgdata.params.use_auto_wb = 0;
        processor->imgdata.params.use_camera_wb = 1;
        for (int channel = 0; channel < 4; ++channel) {
            processor->imgdata.params.user_mul[channel] = 0.0f;
        }

        if (requestedTemperatureKelvin > 0
            && (requestedTemperatureKelvin != cameraTemperatureKelvin
                || requestedTint != 0)) {
            float userMultipliers[4] = {};
            if (cameraTemperatureKelvin <= 0
                || !sonyWhiteBalanceMultipliersForDevelopState(
                    processor->imgdata.color,
                    calibrations,
                    cameraTemperatureKelvin,
                    requestedTemperatureKelvin,
                    requestedTint,
                    userMultipliers)) {
                setError(error,
                         QStringLiteral("Sony white-balance calibration data is unavailable for this RAW file."));
                return false;
            }

            processor->imgdata.params.use_camera_wb = 0;
            for (int channel = 0; channel < 4; ++channel) {
                processor->imgdata.params.user_mul[channel] = userMultipliers[channel];
            }
        }

        int result = processor->dcraw_process();
        if (result != LIBRAW_SUCCESS) {
            processor->free_image();
            setError(error, libRawError("Rendering ARW preview", result));
            return false;
        }

        int memoryResult = LIBRAW_SUCCESS;
        libraw_processed_image_t *processed =
            processor->dcraw_make_mem_image(&memoryResult);
        if (processed == nullptr) {
            processor->free_image();
            setError(error, memoryResult == LIBRAW_SUCCESS
                ? QStringLiteral("LibRaw could not create a linear ARW preview bitmap.")
                : libRawError("Copying ARW preview pixels", memoryResult));
            return false;
        }

        const bool cached = cacheLinearPreview(*processed, error);
        LibRaw::dcraw_clear_mem(processed);
        processor->free_image();

        if (!cached) {
            return false;
        }

        cachedTemperatureKelvin = requestedTemperatureKelvin;
        cachedTint = requestedTint;
        cachedCalibration = calibration;
        return true;
    }

    static bool invertMatrix(const Matrix3 &matrix, Matrix3 &inverse)
    {
        const double a = matrix[0];
        const double b = matrix[1];
        const double c = matrix[2];
        const double d = matrix[3];
        const double e = matrix[4];
        const double f = matrix[5];
        const double g = matrix[6];
        const double h = matrix[7];
        const double i = matrix[8];

        const double determinant = a * (e * i - f * h)
            - b * (d * i - f * g)
            + c * (d * h - e * g);
        if (!std::isfinite(determinant) || std::abs(determinant) <= 1e-12) {
            return false;
        }

        const double reciprocal = 1.0 / determinant;
        inverse = {
            (e * i - f * h) * reciprocal,
            (c * h - b * i) * reciprocal,
            (b * f - c * e) * reciprocal,
            (f * g - d * i) * reciprocal,
            (a * i - c * g) * reciprocal,
            (c * d - a * f) * reciprocal,
            (d * h - e * g) * reciprocal,
            (b * g - a * h) * reciprocal,
            (a * e - b * d) * reciprocal,
        };
        return true;
    }

    bool whiteBalanceMultipliers(int temperatureKelvin,
                                 int tint,
                                 std::array<double, 3> &multipliers) const
    {
        if (!processor) {
            return false;
        }

        const int requestedTemperatureKelvin = temperatureKelvin > 0
            ? std::clamp(temperatureKelvin,
                         kSonyTemperatureMinimumKelvin,
                         kSonyTemperatureMaximumKelvin)
            : 0;
        const int requestedTint = std::clamp(tint, kTintMinimum, kTintMaximum);

        float customMultipliers[4] = {};
        const float *sourceMultipliers = processor->imgdata.color.cam_mul;

        if (requestedTemperatureKelvin > 0
            && (requestedTemperatureKelvin != cameraTemperatureKelvin
                || requestedTint != 0)) {
            if (cameraTemperatureKelvin <= 0
                || !sonyWhiteBalanceMultipliersForDevelopState(
                    processor->imgdata.color,
                    calibrations,
                    cameraTemperatureKelvin,
                    requestedTemperatureKelvin,
                    requestedTint,
                    customMultipliers)) {
                return false;
            }
            sourceMultipliers = customMultipliers;
        }

        const double green = sourceMultipliers[1] > 0.0f
            ? static_cast<double>(sourceMultipliers[1])
            : static_cast<double>(sourceMultipliers[3]);
        const double red = static_cast<double>(sourceMultipliers[0]);
        const double blue = static_cast<double>(sourceMultipliers[2]);
        if (!(red > 0.0) || !(green > 0.0) || !(blue > 0.0)) {
            return false;
        }

        multipliers = {red / green, 1.0, blue / green};
        return std::isfinite(multipliers[0]) && std::isfinite(multipliers[2]);
    }

    bool interactivePreviewParameters(int targetTemperatureKelvin,
                                      int tint,
                                      const RawCalibration &calibration,
                                      Matrix3 &cameraToLinearSrgb,
                                      Matrix3 &linearSrgbToCamera,
                                      Matrix3 &targetCameraToLinearSrgb,
                                      std::array<double, 3> &multiplierRatios) const
    {
        if (!processor
            || cachedImage == nullptr
            || !cachedCalibration.isNeutral()
            || processor->imgdata.idata.colors != 3) {
            return false;
        }

        std::array<double, 3> sourceMultipliers{};
        std::array<double, 3> targetMultipliers{};
        if (!whiteBalanceMultipliers(cachedTemperatureKelvin,
                                     cachedTint,
                                     sourceMultipliers)
            || !whiteBalanceMultipliers(targetTemperatureKelvin,
                                        tint,
                                        targetMultipliers)) {
            return false;
        }

        for (int channel = 0; channel < 3; ++channel) {
            if (!(sourceMultipliers[channel] > 0.0)) {
                return false;
            }
            multiplierRatios[channel] =
                targetMultipliers[channel] / sourceMultipliers[channel];
            if (!std::isfinite(multiplierRatios[channel])
                || !(multiplierRatios[channel] > 0.0)) {
                return false;
            }
        }

        for (int row = 0; row < 3; ++row) {
            for (int column = 0; column < 3; ++column) {
                const double value = processor->imgdata.color.rgb_cam[row][column];
                if (!std::isfinite(value)) {
                    return false;
                }
                cameraToLinearSrgb[row * 3 + column] = value;
            }
        }

        if (!invertMatrix(cameraToLinearSrgb, linearSrgbToCamera)) {
            return false;
        }

        targetCameraToLinearSrgb = cameraToLinearSrgb;
        adjustPrimaryColumn(targetCameraToLinearSrgb,
                            0,
                            calibration.redPrimaryHue,
                            calibration.redPrimarySaturation);
        adjustPrimaryColumn(targetCameraToLinearSrgb,
                            1,
                            calibration.greenPrimaryHue,
                            calibration.greenPrimarySaturation);
        adjustPrimaryColumn(targetCameraToLinearSrgb,
                            2,
                            calibration.bluePrimaryHue,
                            calibration.bluePrimarySaturation);
        return true;
    }

    bool ensureTemperaturePreviewImage(QString *error)
    {
        if (cachedImage == nullptr) {
            return false;
        }

        if (temperaturePreviewImage != nullptr
            && temperaturePreviewImage->width == cachedImage->width
            && temperaturePreviewImage->height == cachedImage->height
            && temperaturePreviewImage->data_size == cachedImage->data_size) {
            return true;
        }

        destroyLinearImage(temperaturePreviewImage);

        const std::size_t allocationBytes =
            offsetof(libraw_processed_image_t, data) + cachedImage->data_size;
        void *storage = ::operator new(allocationBytes, std::nothrow);
        if (storage == nullptr) {
            setError(error,
                     QStringLiteral("Not enough memory to create the interactive develop preview."));
            return false;
        }

        temperaturePreviewImage = ::new (storage) libraw_processed_image_t{};
        temperaturePreviewImage->type = cachedImage->type;
        temperaturePreviewImage->width = cachedImage->width;
        temperaturePreviewImage->height = cachedImage->height;
        temperaturePreviewImage->colors = cachedImage->colors;
        temperaturePreviewImage->bits = cachedImage->bits;
        temperaturePreviewImage->data_size = cachedImage->data_size;
        return true;
    }

    libraw_processed_image_t *buildInteractivePreview(int temperatureKelvin,
                                                       int tint,
                                                       const RawCalibration &calibration,
                                                       QString *error)
    {
        Matrix3 cameraToLinearSrgb{};
        Matrix3 linearSrgbToCamera{};
        Matrix3 targetCameraToLinearSrgb{};
        std::array<double, 3> multiplierRatios{};
        if (!interactivePreviewParameters(temperatureKelvin,
                                          tint,
                                          calibration,
                                          cameraToLinearSrgb,
                                          linearSrgbToCamera,
                                          targetCameraToLinearSrgb,
                                          multiplierRatios)
            || !ensureTemperaturePreviewImage(error)) {
            return nullptr;
        }

        const double shadowTintAmount =
            static_cast<double>(clampedCalibrationValue(calibration.shadowsTint)) / 100.0;
        const std::size_t pixelCount =
            static_cast<std::size_t>(cachedImage->width) * cachedImage->height;
        const auto *source = reinterpret_cast<const std::uint16_t *>(cachedImage->data);
        auto *destination =
            reinterpret_cast<std::uint16_t *>(temperaturePreviewImage->data);

        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const std::size_t offset = pixel * 3;
            const double sourceRed = source[offset];
            const double sourceGreen = source[offset + 1];
            const double sourceBlue = source[offset + 2];

            std::array<double, 3> camera{
                linearSrgbToCamera[0] * sourceRed
                    + linearSrgbToCamera[1] * sourceGreen
                    + linearSrgbToCamera[2] * sourceBlue,
                linearSrgbToCamera[3] * sourceRed
                    + linearSrgbToCamera[4] * sourceGreen
                    + linearSrgbToCamera[5] * sourceBlue,
                linearSrgbToCamera[6] * sourceRed
                    + linearSrgbToCamera[7] * sourceGreen
                    + linearSrgbToCamera[8] * sourceBlue,
            };

            for (int channel = 0; channel < 3; ++channel) {
                camera[channel] *= multiplierRatios[channel];
            }

            if (shadowTintAmount != 0.0) {
                const double baselineRed = cameraToLinearSrgb[0] * camera[0]
                    + cameraToLinearSrgb[1] * camera[1]
                    + cameraToLinearSrgb[2] * camera[2];
                const double baselineGreen = cameraToLinearSrgb[3] * camera[0]
                    + cameraToLinearSrgb[4] * camera[1]
                    + cameraToLinearSrgb[5] * camera[2];
                const double baselineBlue = cameraToLinearSrgb[6] * camera[0]
                    + cameraToLinearSrgb[7] * camera[1]
                    + cameraToLinearSrgb[8] * camera[2];
                const double baselineLuminance = std::max(
                    0.0,
                    (baselineRed * kLinearSrgbRedLuminance
                     + baselineGreen * kLinearSrgbGreenLuminance
                     + baselineBlue * kLinearSrgbBlueLuminance)
                        / static_cast<double>(kLinearMaximum));
                const double weight = shadowTintWeight(baselineLuminance);

                if (weight > 0.0) {
                    const double shift = shadowTintAmount
                        * kShadowTintMaximumLogShift
                        * weight;
                    const double redBlueGain = std::exp(0.5 * shift);
                    const double greenGain = std::exp(-shift);
                    camera[0] *= redBlueGain;
                    camera[1] *= greenGain;
                    camera[2] *= redBlueGain;
                }
            }

            const double transformedRed = targetCameraToLinearSrgb[0] * camera[0]
                + targetCameraToLinearSrgb[1] * camera[1]
                + targetCameraToLinearSrgb[2] * camera[2];
            const double transformedGreen = targetCameraToLinearSrgb[3] * camera[0]
                + targetCameraToLinearSrgb[4] * camera[1]
                + targetCameraToLinearSrgb[5] * camera[2];
            const double transformedBlue = targetCameraToLinearSrgb[6] * camera[0]
                + targetCameraToLinearSrgb[7] * camera[1]
                + targetCameraToLinearSrgb[8] * camera[2];

            destination[offset] = static_cast<std::uint16_t>(std::lround(
                std::clamp(transformedRed,
                           0.0,
                           static_cast<double>(kLinearMaximum))));
            destination[offset + 1] = static_cast<std::uint16_t>(std::lround(
                std::clamp(transformedGreen,
                           0.0,
                           static_cast<double>(kLinearMaximum))));
            destination[offset + 2] = static_cast<std::uint16_t>(std::lround(
                std::clamp(transformedBlue,
                           0.0,
                           static_cast<double>(kLinearMaximum))));
        }

        return temperaturePreviewImage;
    }

    bool cachedPreviewMatches(int temperatureKelvin,
                              int tint,
                              const RawCalibration &calibration) const
    {
        const int requestedTemperatureKelvin = temperatureKelvin > 0
            ? std::clamp(temperatureKelvin,
                         kSonyTemperatureMinimumKelvin,
                         kSonyTemperatureMaximumKelvin)
            : 0;
        const int requestedTint = std::clamp(tint, kTintMinimum, kTintMaximum);

        return cachedImage != nullptr
            && cachedTemperatureKelvin == requestedTemperatureKelvin
            && cachedTint == requestedTint
            && cachedCalibration == calibration;
    }

    QString filePath;
    std::unique_ptr<CalibratedLibRaw> processor;
    std::vector<TemperatureCalibration> calibrations;
    int cameraTemperatureKelvin = 0;
    libraw_processed_image_t *cachedImage = nullptr;
    libraw_processed_image_t *temperaturePreviewImage = nullptr;
    int cachedTemperatureKelvin = std::numeric_limits<int>::min();
    int cachedTint = std::numeric_limits<int>::min();
    RawCalibration cachedCalibration;
};

ArwPreviewSession::ArwPreviewSession()
    : m_impl(std::make_unique<Impl>())
{
}

ArwPreviewSession::~ArwPreviewSession() = default;

bool ArwPreviewSession::prepare(const QString &filePath,
                                int whiteBalanceTemperatureKelvin,
                                int tint,
                                const RawCalibration &calibration,
                                QString *error)
{
    Q_UNUSED(calibration);

    if (error != nullptr) {
        error->clear();
    }

    // Keep a neutral linear preview cached. Calibration can then be applied to
    // that 1280px buffer interactively instead of rerunning LibRaw for every
    // slider position. Full-resolution commits still use CalibratedLibRaw.
    return m_impl->ensureRaw(filePath, error)
        && m_impl->refreshWhiteBalance(whiteBalanceTemperatureKelvin,
                                       tint,
                                       RawCalibration{},
                                       error);
}

QImage ArwPreviewSession::render(const QString &filePath,
                                 QString *error,
                                 int whiteBalanceTemperatureKelvin,
                                 int tint,
                                 const RawCalibration &calibration,
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
                                 int vibrance)
{
    if (error != nullptr) {
        error->clear();
    }

    if (!m_impl->ensureRaw(filePath, error)) {
        return {};
    }

    // Calibration previews are generated from a neutral, linear, half-size
    // cache. This keeps dragging responsive while the full render path remains
    // unchanged and authoritative on commit.
    if (m_impl->cachedImage == nullptr || !m_impl->cachedCalibration.isNeutral()) {
        if (!m_impl->refreshWhiteBalance(whiteBalanceTemperatureKelvin,
                                         tint,
                                         RawCalibration{},
                                         error)) {
            return {};
        }
    }

    libraw_processed_image_t *linearPreview = nullptr;
    if (m_impl->cachedPreviewMatches(whiteBalanceTemperatureKelvin,
                                     tint,
                                     calibration)) {
        linearPreview = m_impl->cachedImage;
    } else {
        linearPreview = m_impl->buildInteractivePreview(whiteBalanceTemperatureKelvin,
                                                        tint,
                                                        calibration,
                                                        error);
    }

    // Preserve the existing slow path as a correctness fallback for unusual
    // RAW color models or matrices that cannot be inverted safely.
    if (linearPreview == nullptr) {
        if (error != nullptr) {
            error->clear();
        }
        if (!m_impl->refreshWhiteBalance(whiteBalanceTemperatureKelvin,
                                         tint,
                                         calibration,
                                         error)) {
            return {};
        }
        linearPreview = m_impl->cachedImage;
    }

    return developedSrgbImage(*linearPreview,
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
}

} // namespace imaging
