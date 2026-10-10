#include "imaging/Histogram.h"

#include <algorithm>

namespace imaging {

quint64 HistogramData::peak() const noexcept
{
    quint64 maximum = 0;
    for (int i = 0; i < kBinCount; ++i) {
        maximum = std::max(maximum, red[static_cast<std::size_t>(i)]);
        maximum = std::max(maximum, green[static_cast<std::size_t>(i)]);
        maximum = std::max(maximum, blue[static_cast<std::size_t>(i)]);
    }
    return maximum;
}

HistogramData Histogram::calculate(const QImage &image)
{
    HistogramData histogram;
    if (image.isNull() || image.width() <= 0 || image.height() <= 0) {
        return histogram;
    }

    // ARW previews already arrive as RGB888. Other Qt-supported raster formats
    // are normalized to the same byte-ordered RGB layout before counting.
    const QImage rgbImage = image.format() == QImage::Format_RGB888
        ? image
        : image.convertToFormat(QImage::Format_RGB888);

    if (rgbImage.isNull()) {
        return histogram;
    }

    histogram.pixelCount = static_cast<quint64>(rgbImage.width())
        * static_cast<quint64>(rgbImage.height());

    for (int y = 0; y < rgbImage.height(); ++y) {
        const uchar *scanLine = rgbImage.constScanLine(y);

        for (int x = 0; x < rgbImage.width(); ++x) {
            const uchar *pixel = scanLine + (x * 3);
            ++histogram.red[pixel[0]];
            ++histogram.green[pixel[1]];
            ++histogram.blue[pixel[2]];
        }
    }

    return histogram;
}

} // namespace imaging
