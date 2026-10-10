#pragma once

#include <QImage>
#include <QString>

namespace imaging {

struct ImageLoadResult {
    QImage image;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept { return !image.isNull(); }
};

class ImageLoader final
{
public:
    ImageLoader() = delete;

    // Bootstrap loader for display images supported by Qt (JPEG/PNG/TIFF/etc.).
    // RAW decoding will live in the image pipeline, not in PhotoCanvas.
    [[nodiscard]] static ImageLoadResult load(const QString &filePath);
    [[nodiscard]] static QString fileDialogFilter();
};

} // namespace imaging
