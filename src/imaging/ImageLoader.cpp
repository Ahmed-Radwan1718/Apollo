#include "imaging/ImageLoader.h"

#include <QImageReader>
#include <QStringList>

#include <algorithm>
#include <utility>

namespace imaging {

ImageLoadResult ImageLoader::load(const QString &filePath)
{
    QImageReader reader(filePath);
    reader.setAutoTransform(true);

    QImage image = reader.read();
    if (image.isNull()) {
        return {{}, reader.errorString()};
    }

    // A photograph's image dimensions are pixel dimensions. QImageReader may assign
    // a device-pixel ratio to files named with Qt's @2x asset convention; that is
    // useful for UI assets but wrong for photo-editor zoom math.
    image.setDevicePixelRatio(1.0);

    return {std::move(image), {}};
}

QString ImageLoader::fileDialogFilter()
{
    QStringList patterns;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    patterns.reserve(formats.size());

    for (const QByteArray &format : formats) {
        const QString suffix = QString::fromLatin1(format).toLower();
        if (!suffix.isEmpty()) {
            patterns.push_back(QStringLiteral("*.%1").arg(suffix));
        }
    }

    patterns.removeDuplicates();
    std::sort(patterns.begin(), patterns.end());

    return QStringLiteral("Supported images (%1);;All files (*.*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

} // namespace imaging
