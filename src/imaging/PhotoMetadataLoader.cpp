#include "imaging/PhotoMetadataLoader.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>

#include <string>

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

} // namespace

PhotoMetadata PhotoMetadataLoader::load(const QString &filePath)
{
    PhotoMetadata metadata;

    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()
        || fileInfo.suffix().compare(QStringLiteral("arw"), Qt::CaseInsensitive) != 0) {
        return metadata;
    }

    LibRaw processor(LIBRAW_OPTIONS_NO_DATAERR_CALLBACK);
    if (openRawFile(processor, fileInfo.absoluteFilePath()) != LIBRAW_SUCCESS) {
        return metadata;
    }

    // LibRaw fills imgdata.other while opening the RAW file, so these values are
    // available without unpacking or demosaicing the sensor data.
    const libraw_imgother_t &other = processor.imgdata.other;
    metadata.shutterSeconds = other.shutter;
    metadata.aperture = other.aperture;
    metadata.iso = other.iso_speed;
    metadata.focalLengthMm = other.focal_len;
    return metadata;
}

} // namespace imaging
