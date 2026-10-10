#include "imaging/ArwDecoder.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QColorSpace>
#include <QFile>

#include <limits>
#include <string>
#include <utility>

namespace imaging {
namespace {

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

} // namespace

QImage ArwDecoder::decode(const QString &filePath,
                          QString *error,
                          PhotoMetadata *metadata)
{
    if (error != nullptr) {
        error->clear();
    }
    if (metadata != nullptr) {
        *metadata = {};
    }

    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);

    // Produce a neutral display rendering from the RAW data itself. Keep automatic
    // brightness disabled so merely opening a file does not silently apply an
    // exposure-like adjustment. The original ARW is never modified.
    processor.imgdata.params.use_camera_wb = 1;
    processor.imgdata.params.use_camera_matrix = 1;
    processor.imgdata.params.output_color = 1; // sRGB
    processor.imgdata.params.output_bps = 8;
    processor.imgdata.params.no_auto_bright = 1;
    processor.imgdata.params.gamm[0] = 1.0 / 2.4;
    processor.imgdata.params.gamm[1] = 12.92;

    int result = openFile(processor, filePath);
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Opening ARW", result));
        return {};
    }

    if (metadata != nullptr) {
        const libraw_imgother_t &other = processor.imgdata.other;
        metadata->shutterSeconds = other.shutter;
        metadata->aperture = other.aperture;
        metadata->iso = other.iso_speed;
        metadata->focalLengthMm = other.focal_len;
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
