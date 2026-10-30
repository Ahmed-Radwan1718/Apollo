#include "imaging/PhotoMetadataLoader.h"

#include <libraw/libraw.h>

#include <QByteArray>
#include <QFile>
#include <QFileInfo>
#include <QStringList>
#include <QXmlStreamReader>

#include <cmath>
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

QString rawString(const char *value)
{
    return value != nullptr ? QString::fromUtf8(value).trimmed() : QString{};
}

double gpsCoordinate(const float *parts, char reference)
{
    double coordinate = static_cast<double>(parts[0])
        + static_cast<double>(parts[1]) / 60.0
        + static_cast<double>(parts[2]) / 3600.0;

    if (reference == 'S' || reference == 's'
        || reference == 'W' || reference == 'w') {
        coordinate = -coordinate;
    }
    return coordinate;
}

struct XmpMetadata {
    QString creator;
    QString copyrightNotice;
    QString description;
    QString keywords;
};

XmpMetadata parseXmpMetadata(const char *data, unsigned length)
{
    XmpMetadata result;
    if (data == nullptr || length == 0) {
        return result;
    }

    enum class Field {
        None,
        Creator,
        Copyright,
        Description,
        Keywords,
    };

    QStringList creators;
    QStringList copyrightNotices;
    QStringList descriptions;
    QStringList keywords;

    const QByteArray payload(data, static_cast<int>(length));
    QXmlStreamReader xml(payload);
    Field field = Field::None;
    QString fieldElement;

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isStartElement()) {
            const QString localName = xml.name().toString();
            if (localName == QStringLiteral("creator")) {
                field = Field::Creator;
                fieldElement = localName;
            } else if (localName == QStringLiteral("rights")) {
                field = Field::Copyright;
                fieldElement = localName;
            } else if (localName == QStringLiteral("description")) {
                field = Field::Description;
                fieldElement = localName;
            } else if (localName == QStringLiteral("subject")) {
                field = Field::Keywords;
                fieldElement = localName;
            } else if (field != Field::None && localName == QStringLiteral("li")) {
                const QString text = xml.readElementText(
                    QXmlStreamReader::SkipChildElements).trimmed();
                if (text.isEmpty()) {
                    continue;
                }

                switch (field) {
                case Field::Creator:
                    creators.push_back(text);
                    break;
                case Field::Copyright:
                    copyrightNotices.push_back(text);
                    break;
                case Field::Description:
                    descriptions.push_back(text);
                    break;
                case Field::Keywords:
                    keywords.push_back(text);
                    break;
                case Field::None:
                    break;
                }
            }
        } else if (xml.isEndElement()
                   && xml.name().toString() == fieldElement) {
            field = Field::None;
            fieldElement.clear();
        }
    }

    if (!creators.isEmpty()) {
        result.creator = creators.join(QStringLiteral(", "));
    }
    if (!copyrightNotices.isEmpty()) {
        result.copyrightNotice = copyrightNotices.join(QStringLiteral("; "));
    }
    if (!descriptions.isEmpty()) {
        result.description = descriptions.constFirst();
    }
    if (!keywords.isEmpty()) {
        keywords.removeDuplicates();
        result.keywords = keywords.join(QStringLiteral(", "));
    }
    return result;
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

    // LibRaw fills these metadata structures while opening the RAW file, so no
    // sensor unpack or demosaic is needed just to collect export metadata.
    const libraw_imgother_t &other = processor.imgdata.other;
    metadata.shutterSeconds = other.shutter;
    metadata.aperture = other.aperture;
    metadata.iso = other.iso_speed;
    metadata.focalLengthMm = other.focal_len;

    metadata.cameraMake = rawString(processor.imgdata.idata.make);
    metadata.cameraModel = rawString(processor.imgdata.idata.model);
    metadata.lensMake = rawString(processor.imgdata.lens.LensMake);
    metadata.lensModel = rawString(processor.imgdata.lens.Lens);

    metadata.captureTimestamp = static_cast<qint64>(other.timestamp);

    const libraw_gps_info_t &gps = other.parsed_gps;
    if (gps.gpsparsed != 0 && gps.latref != '\0' && gps.longref != '\0') {
        metadata.hasLocation = true;
        metadata.latitude = gpsCoordinate(gps.latitude, gps.latref);
        metadata.longitude = gpsCoordinate(gps.longitude, gps.longref);
        metadata.hasAltitude = std::isfinite(static_cast<double>(gps.altitude));
        metadata.altitudeMeters = gps.altref == 1
            ? -static_cast<double>(gps.altitude)
            : static_cast<double>(gps.altitude);
    }

    metadata.creator = rawString(other.artist);
    metadata.description = rawString(other.desc);

    const XmpMetadata xmp = parseXmpMetadata(processor.imgdata.idata.xmpdata,
                                             processor.imgdata.idata.xmplen);
    if (metadata.creator.isEmpty()) {
        metadata.creator = xmp.creator;
    }
    metadata.copyrightNotice = xmp.copyrightNotice;
    if (metadata.description.isEmpty()) {
        metadata.description = xmp.description;
    }
    metadata.keywords = xmp.keywords;

    metadata.cameraSerial = rawString(processor.imgdata.shootinginfo.BodySerial);
    metadata.lensSerial = rawString(processor.imgdata.lens.LensSerial);

    return metadata;
}

} // namespace imaging
