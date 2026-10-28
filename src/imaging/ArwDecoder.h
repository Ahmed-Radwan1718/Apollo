#pragma once

#include "imaging/PhotoMetadata.h"
#include "imaging/RawCalibration.h"

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
                                       int highlights = 0,
                                       int shadows = 0,
                                       int whites = 0,
                                       int blacks = 0,
                                       int contrast = 0,
                                       int texture = 0,
                                       int clarity = 0,
                                       int dehaze = 0,
                                       int saturation = 0,
                                       int vibrance = 0,
                                       const RawCalibration &calibration = RawCalibration{},
                                       bool halfSizePreview = false);

    [[nodiscard]] static WhiteBalancePickResult pickWhiteBalance(
        const QString &filePath,
        const QPointF &normalizedPosition);
};

} // namespace imaging
