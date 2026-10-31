#pragma once

#include "imaging/ColorMixerSettings.h"
#include "imaging/RawCalibration.h"

#include <QImage>
#include <QString>

#include <memory>

namespace imaging {

class ArwPreviewSession final
{
public:
    ArwPreviewSession();
    ~ArwPreviewSession();

    ArwPreviewSession(const ArwPreviewSession &) = delete;
    ArwPreviewSession &operator=(const ArwPreviewSession &) = delete;

    [[nodiscard]] bool prepare(const QString &filePath,
                               int whiteBalanceTemperatureKelvin,
                               int tint,
                               const RawCalibration &calibration,
                               QString *error = nullptr);

    [[nodiscard]] QImage render(const QString &filePath,
                                QString *error,
                                int whiteBalanceTemperatureKelvin,
                                int tint,
                                const RawCalibration &calibration,
                                int exposureHundredths,
                                int highlights,
                                int shadows,
                                int whites,
                                int blacks,
                                int contrast,
                                int texture,
                                int clarity,
                                int dehaze,
                                int saturation,
                                int vibrance,
                                const ColorMixerSettings &colorMixer);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace imaging
