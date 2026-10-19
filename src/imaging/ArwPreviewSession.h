#pragma once

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
                               QString *error = nullptr);

    [[nodiscard]] QImage render(const QString &filePath,
                                QString *error,
                                int whiteBalanceTemperatureKelvin,
                                int tint,
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
                                int vibrance);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace imaging
