#include "imaging/AvifEncoder.h"

#include <avif/avif.h>

#include <QImage>
#include <QSaveFile>
#include <QThread>

#include <algorithm>
#include <cstdint>
#include <memory>

namespace imaging {
namespace {

struct AvifOutputData {
    avifRWData data = AVIF_DATA_EMPTY;

    ~AvifOutputData()
    {
        avifRWDataFree(&data);
    }
};

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QString avifError(const char *context, avifResult result)
{
    return QStringLiteral("%1: %2")
        .arg(QString::fromLatin1(context),
             QString::fromLatin1(avifResultToString(result)));
}

} // namespace

bool AvifEncoder::write(const QImage &image,
                        const QString &filePath,
                        int quality,
                        const QByteArray &xmpMetadata,
                        QString *error)
{
    if (image.isNull()) {
        setError(error, QStringLiteral("The rendered image is empty."));
        return false;
    }

    const QImage rgba = image.convertToFormat(QImage::Format_RGBA8888);
    if (rgba.isNull()) {
        setError(error, QStringLiteral("Apollo could not prepare the image for AVIF encoding."));
        return false;
    }

    using AvifImagePtr = std::unique_ptr<avifImage, decltype(&avifImageDestroy)>;
    AvifImagePtr avifImage(
        avifImageCreate(static_cast<uint32_t>(rgba.width()),
                        static_cast<uint32_t>(rgba.height()),
                        8,
                        AVIF_PIXEL_FORMAT_YUV444),
        &avifImageDestroy);
    if (!avifImage) {
        setError(error, QStringLiteral("Apollo could not allocate the AVIF image."));
        return false;
    }

    avifImage->colorPrimaries = AVIF_COLOR_PRIMARIES_BT709;
    avifImage->transferCharacteristics = AVIF_TRANSFER_CHARACTERISTICS_SRGB;
    avifImage->matrixCoefficients = AVIF_MATRIX_COEFFICIENTS_BT709;
    avifImage->yuvRange = AVIF_RANGE_FULL;

    avifRGBImage rgb;
    avifRGBImageSetDefaults(&rgb, avifImage.get());
    rgb.depth = 8;
    rgb.format = AVIF_RGB_FORMAT_RGBA;
    rgb.pixels = const_cast<uint8_t *>(rgba.constBits());
    rgb.rowBytes = static_cast<uint32_t>(rgba.bytesPerLine());

    const avifResult conversionResult = avifImageRGBToYUV(avifImage.get(), &rgb);
    if (conversionResult != AVIF_RESULT_OK) {
        setError(error, avifError("AVIF color conversion failed", conversionResult));
        return false;
    }

    if (!xmpMetadata.isEmpty()) {
        const avifResult metadataResult = avifImageSetMetadataXMP(
            avifImage.get(),
            reinterpret_cast<const uint8_t *>(xmpMetadata.constData()),
            static_cast<size_t>(xmpMetadata.size()));
        if (metadataResult != AVIF_RESULT_OK) {
            setError(error, avifError("AVIF metadata could not be attached", metadataResult));
            return false;
        }
    }

    using AvifEncoderPtr = std::unique_ptr<avifEncoder, decltype(&avifEncoderDestroy)>;
    AvifEncoderPtr encoder(avifEncoderCreate(), &avifEncoderDestroy);
    if (!encoder) {
        setError(error, QStringLiteral("Apollo could not allocate the AVIF encoder."));
        return false;
    }

    encoder->maxThreads = std::clamp(QThread::idealThreadCount(), 1, 64);
    encoder->quality = std::clamp(quality, 1, 100);
    encoder->qualityAlpha = AVIF_QUALITY_LOSSLESS;
    encoder->speed = 6;

    AvifOutputData output;
    const avifResult encodeResult = avifEncoderWrite(
        encoder.get(), avifImage.get(), &output.data);
    if (encodeResult != AVIF_RESULT_OK) {
        QString message = avifError("AVIF encoding failed", encodeResult);
        if (encoder->diag.error[0] != '\0') {
            message += QStringLiteral(" (%1)")
                .arg(QString::fromUtf8(encoder->diag.error));
        }
        setError(error, message);
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }

    const qint64 byteCount = static_cast<qint64>(output.data.size);
    if (file.write(reinterpret_cast<const char *>(output.data.data), byteCount) != byteCount) {
        setError(error, file.errorString());
        file.cancelWriting();
        return false;
    }

    if (!file.commit()) {
        setError(error, file.errorString());
        return false;
    }

    return true;
}

} // namespace imaging
