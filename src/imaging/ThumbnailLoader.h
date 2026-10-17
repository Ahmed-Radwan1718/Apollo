#pragma once

#include <QImage>
#include <QString>

namespace imaging {

class ThumbnailLoader final
{
public:
    ThumbnailLoader() = delete;

    [[nodiscard]] static QImage load(const QString &filePath,
                                     int maximumEdgeLength = 640,
                                     QString *error = nullptr);
};

} // namespace imaging
