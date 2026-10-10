#pragma once

#include "imaging/PhotoMetadata.h"

#include <QImage>
#include <QString>

namespace imaging {

struct ImageLoadResult {
    QImage image;
    PhotoMetadata metadata;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept { return !image.isNull(); }
};

class ImageLoader final
{
public:
    ImageLoader() = delete;

    // Loads Sony ARW through LibRaw and regular raster formats through Qt.
    [[nodiscard]] static ImageLoadResult load(const QString &filePath);
    [[nodiscard]] static QString fileDialogFilter();
};

} // namespace imaging
