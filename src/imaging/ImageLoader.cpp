#include "imaging/ImageLoader.h"

#include "imaging/ArwDecoder.h"

#include <QFileInfo>
#include <QHash>
#include <QImageReader>
#include <QStringList>

#include <algorithm>
#include <mutex>
#include <optional>
#include <utility>

namespace imaging {
namespace {

constexpr int kDecodedImageCacheEntries = 2;

std::mutex g_decodedImageCacheMutex;
QHash<QString, ImageLoadResult> g_decodedImageCache;
QStringList g_decodedImageCacheOrder;

QString normalizedImagePath(const QString &filePath)
{
    return QFileInfo(filePath).absoluteFilePath();
}

std::optional<ImageLoadResult> cachedImage(const QString &filePath)
{
    std::lock_guard lock(g_decodedImageCacheMutex);

    const auto iterator = g_decodedImageCache.constFind(filePath);
    if (iterator == g_decodedImageCache.constEnd()) {
        return std::nullopt;
    }

    g_decodedImageCacheOrder.removeAll(filePath);
    g_decodedImageCacheOrder.push_back(filePath);
    return *iterator;
}

void cacheImage(const QString &filePath, const ImageLoadResult &result)
{
    if (filePath.isEmpty() || !result.succeeded()) {
        return;
    }

    std::lock_guard lock(g_decodedImageCacheMutex);

    g_decodedImageCache.insert(filePath, result);
    g_decodedImageCacheOrder.removeAll(filePath);
    g_decodedImageCacheOrder.push_back(filePath);

    while (g_decodedImageCacheOrder.size() > kDecodedImageCacheEntries) {
        const QString oldestPath = g_decodedImageCacheOrder.takeFirst();
        g_decodedImageCache.remove(oldestPath);
    }
}

} // namespace

ImageLoadResult ImageLoader::load(const QString &filePath,
                                  int whiteBalanceTemperatureKelvin)
{
    const QString absoluteFilePath = normalizedImagePath(filePath);

    // Only the camera/default rendering is cached. Explicit white-balance loads
    // represent a different develop state and must continue through the decoder.
    if (whiteBalanceTemperatureKelvin == 0) {
        if (const std::optional<ImageLoadResult> cached = cachedImage(absoluteFilePath)) {
            return *cached;
        }
    }

    ImageLoadResult result;

    if (QFileInfo(absoluteFilePath).suffix().compare(QStringLiteral("arw"),
                                                     Qt::CaseInsensitive) == 0) {
        QString error;
        PhotoMetadata metadata;
        QImage image = ArwDecoder::decode(absoluteFilePath,
                                          &error,
                                          &metadata,
                                          whiteBalanceTemperatureKelvin);
        result = {std::move(image), metadata, std::move(error)};
    } else {
        QImageReader reader(absoluteFilePath);
        reader.setAutoTransform(true);

        QImage image = reader.read();
        if (image.isNull()) {
            return {{}, {}, reader.errorString()};
        }

        // A photograph's image dimensions are pixel dimensions. QImageReader may assign
        // a device-pixel ratio to files named with Qt's @2x asset convention; that is
        // useful for UI assets but wrong for photo-editor zoom math.
        image.setDevicePixelRatio(1.0);
        result = {std::move(image), {}, {}};
    }

    if (whiteBalanceTemperatureKelvin == 0 && result.succeeded()) {
        cacheImage(absoluteFilePath, result);
    }

    return result;
}

QString ImageLoader::fileDialogFilter()
{
    QStringList patterns;
    const QList<QByteArray> formats = QImageReader::supportedImageFormats();
    patterns.reserve(formats.size() + 1);

    patterns.push_back(QStringLiteral("*.arw"));

    for (const QByteArray &format : formats) {
        const QString suffix = QString::fromLatin1(format).toLower();
        if (!suffix.isEmpty()) {
            patterns.push_back(QStringLiteral("*.%1").arg(suffix));
        }
    }

    patterns.removeDuplicates();
    std::sort(patterns.begin(), patterns.end());

    return QStringLiteral("Supported images (%1);;Sony RAW (*.arw);;All files (*.*)")
        .arg(patterns.join(QLatin1Char(' ')));
}

} // namespace imaging
