#pragma once

#include <QByteArray>
#include <QString>

class QImage;

namespace imaging {

class AvifEncoder final
{
public:
    static bool write(const QImage &image,
                      const QString &filePath,
                      int quality,
                      const QByteArray &xmpMetadata,
                      QString *error = nullptr);
};

} // namespace imaging
