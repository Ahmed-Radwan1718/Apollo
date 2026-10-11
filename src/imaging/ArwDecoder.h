#pragma once

#include "imaging/PhotoMetadata.h"

#include <QImage>
#include <QPointF>
#include <QString>

namespace imaging {

struct WhiteBalancePickResult {
    int temperatureKelvin = 0;
    int tint = 0;
    QString error;

    [[nodiscard]] bool succeeded() const noexcept
    {
        return error.isEmpty() && temperatureKelvin > 0;
    }
};

class ArwDecoder final
{
public:
    ArwDecoder() = delete;

    [[nodiscard]] static QImage decode(const QString &filePath,
                                       QString *error = nullptr,
                                       PhotoMetadata *metadata = nullptr,
                                       int whiteBalanceTemperatureKelvin = 0,
                                       int tint = 0,
                                       int exposureHundredths = 0,
                                       bool halfSizePreview = false);

    [[nodiscard]] static WhiteBalancePickResult pickWhiteBalance(
        const QString &filePath,
        const QPointF &normalizedPosition);
};

} // namespace imaging
