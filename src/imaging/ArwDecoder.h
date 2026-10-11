#pragma once

#include "imaging/PhotoMetadata.h"

#include <QImage>
#include <QString>

namespace imaging {

class ArwDecoder final
{
public:
    ArwDecoder() = delete;

    [[nodiscard]] static QImage decode(const QString &filePath,
                                       QString *error = nullptr,
                                       PhotoMetadata *metadata = nullptr,
                                       int whiteBalanceTemperatureKelvin = 0,
                                       int exposureHundredths = 0,
                                       bool halfSizePreview = false);
};

} // namespace imaging
