#include "imaging/ArwPreviewSession.h"

#include <cstddef>
#include <cstring>
#include <memory>

namespace imaging {

struct ArwPreviewSession::Impl
{
    static constexpr int kMaximumPreviewLongSide = 1280;

    ~Impl()
    {
        clearCachedImage();
    }

    void clearCachedImage()
    {
        if (cachedImage != nullptr) {
            std::destroy_at(cachedImage);
            ::operator delete(cachedImage);
            cachedImage = nullptr;
        }
        cachedTemperatureKelvin = std::numeric_limits<int>::min();
        cachedTint = std::numeric_limits<int>::min();
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

        auto candidate = std::make_unique<LibRaw>(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
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
            && cachedTint == requestedTint) {
            return true;
        }

        processor->free_image();
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
        return true;
    }

    QString filePath;
    std::unique_ptr<LibRaw> processor;
    std::vector<TemperatureCalibration> calibrations;
    int cameraTemperatureKelvin = 0;
    libraw_processed_image_t *cachedImage = nullptr;
    int cachedTemperatureKelvin = std::numeric_limits<int>::min();
    int cachedTint = std::numeric_limits<int>::min();
};

ArwPreviewSession::ArwPreviewSession()
    : m_impl(std::make_unique<Impl>())
{
}

ArwPreviewSession::~ArwPreviewSession() = default;

bool ArwPreviewSession::prepare(const QString &filePath,
                                int whiteBalanceTemperatureKelvin,
                                int tint,
                                QString *error)
{
    if (error != nullptr) {
        error->clear();
    }

    return m_impl->ensureRaw(filePath, error)
        && m_impl->refreshWhiteBalance(whiteBalanceTemperatureKelvin, tint, error);
}

QImage ArwPreviewSession::render(const QString &filePath,
                                 QString *error,
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
                                 int vibrance)
{
    if (!prepare(filePath, whiteBalanceTemperatureKelvin, tint, error)) {
        return {};
    }

    return developedSrgbImage(*m_impl->cachedImage,
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
