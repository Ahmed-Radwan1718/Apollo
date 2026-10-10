#pragma once

#include <QImage>
#include <QString>

namespace imaging {

class ArwDecoder final
{
public:
    ArwDecoder() = delete;

    [[nodiscard]] static QImage decode(const QString &filePath,
                                       QString *error = nullptr);
};

} // namespace imaging
