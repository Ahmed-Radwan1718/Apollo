#include "imaging/ThumbnailLoader.h"

#include <libraw/libraw.h>

#include <QBuffer>
#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QImageReader>
#include <QIODevice>
#include <QSize>

#include <cstddef>
#include <string>
#include <utility>

namespace imaging {
namespace {

int openRawFile(LibRaw &processor, const QString &filePath)
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

QString libRawError(const char *stage, int errorCode)
{
    return QStringLiteral("%1 failed: %2")
        .arg(QString::fromLatin1(stage),
             QString::fromLatin1(LibRaw::strerror(errorCode)));
}

void setError(QString *error, QString message)
{
    if (error) {
        *error = std::move(message);
    }
}

QImage limitThumbnailSize(QImage image, int maximumEdgeLength)
{
    if (image.isNull() || maximumEdgeLength <= 0) {
        return image;
    }

    if (image.width() > maximumEdgeLength || image.height() > maximumEdgeLength) {
        image = image.scaled(maximumEdgeLength,
                             maximumEdgeLength,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }

    image.setDevicePixelRatio(1.0);
    return image;
}

QImage loadRawThumbnail(const QString &filePath,
                        int maximumEdgeLength,
                        QString *error)
{
    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);

    int result = openRawFile(processor, filePath);
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Opening RAW thumbnail", result));
        return {};
    }

    result = processor.unpack_thumb();
    if (result != LIBRAW_SUCCESS) {
        setError(error, libRawError("Extracting RAW thumbnail", result));
        return {};
    }

    int memoryResult = LIBRAW_SUCCESS;
    libraw_processed_image_t *processed = processor.dcraw_make_mem_thumb(&memoryResult);
    if (!processed) {
        setError(error,
                 memoryResult == LIBRAW_SUCCESS
                     ? QStringLiteral("LibRaw could not create the embedded RAW thumbnail.")
                     : libRawError("Copying RAW thumbnail", memoryResult));
        return {};
    }

    QImage image;

    if (processed->type == LIBRAW_IMAGE_JPEG) {
        const QByteArray encoded(
            reinterpret_cast<const char *>(processed->data),
            static_cast<qsizetype>(processed->data_size));
        QBuffer buffer;
        buffer.setData(encoded);
        buffer.open(QIODevice::ReadOnly);

        QImageReader reader(&buffer);
        reader.setAutoTransform(true);
        image = reader.read();
    } else if (processed->type == LIBRAW_IMAGE_BITMAP
               && processed->bits == 8
               && processed->colors == 3
               && processed->width > 0
               && processed->height > 0) {
        const int width = static_cast<int>(processed->width);
        const int height = static_cast<int>(processed->height);
        const qsizetype bytesPerLine = static_cast<qsizetype>(width) * 3;
        const qsizetype requiredBytes = bytesPerLine * height;

        if (static_cast<qsizetype>(processed->data_size) >= requiredBytes) {
            const QImage wrapped(processed->data,
                                 width,
                                 height,
                                 bytesPerLine,
                                 QImage::Format_RGB888);
            image = wrapped.copy();
        }
    }

    LibRaw::dcraw_clear_mem(processed);

    if (image.isNull()) {
        setError(error, QStringLiteral("The embedded RAW thumbnail could not be decoded."));
        return {};
    }

    return limitThumbnailSize(std::move(image), maximumEdgeLength);
}

QImage loadStandardThumbnail(const QString &filePath,
                             int maximumEdgeLength,
                             QString *error)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    const QSize sourceSize = reader.size();
    if (sourceSize.isValid()
        && maximumEdgeLength > 0
        && (sourceSize.width() > maximumEdgeLength
            || sourceSize.height() > maximumEdgeLength)) {
        reader.setScaledSize(sourceSize.scaled(QSize(maximumEdgeLength,
                                                     maximumEdgeLength),
                                               Qt::KeepAspectRatio));
    }

    QImage image = reader.read();
    if (image.isNull()) {
        setError(error, reader.errorString());
        return {};
    }

    return limitThumbnailSize(std::move(image), maximumEdgeLength);
}

} // namespace

QImage ThumbnailLoader::load(const QString &filePath,
                             int maximumEdgeLength,
                             QString *error)
{
    if (error) {
        error->clear();
    }

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) {
        setError(error, QStringLiteral("The photo file does not exist."));
        return {};
    }

    if (fileInfo.suffix().compare(QStringLiteral("arw"), Qt::CaseInsensitive) == 0) {
        return loadRawThumbnail(fileInfo.absoluteFilePath(), maximumEdgeLength, error);
    }

    return loadStandardThumbnail(fileInfo.absoluteFilePath(), maximumEdgeLength, error);
}

} // namespace imaging
