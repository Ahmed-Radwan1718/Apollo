#pragma once

#include <QImage>
#include <QString>

#include <cstdint>
#include <memory>

class QOffscreenSurface;

namespace imaging {

class GpuDevelopPreviewRenderer final
{
public:
    explicit GpuDevelopPreviewRenderer(QOffscreenSurface *surface = nullptr);
    ~GpuDevelopPreviewRenderer();

    GpuDevelopPreviewRenderer(const GpuDevelopPreviewRenderer &) = delete;
    GpuDevelopPreviewRenderer &operator=(const GpuDevelopPreviewRenderer &) = delete;

    [[nodiscard]] bool isAvailable() const noexcept;

    [[nodiscard]] QImage render(const std::uint16_t *samples,
                                int width,
                                int height,
                                int whitePoint,
                                std::uint64_t sourceRevision,
                                int exposureHundredths,
                                int highlights,
                                int shadows,
                                int whites,
                                int blacks,
                                int contrast,
                                int saturation,
                                QString *error = nullptr);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace imaging
