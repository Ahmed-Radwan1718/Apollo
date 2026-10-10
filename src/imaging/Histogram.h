#pragma once

#include <QImage>
#include <QtGlobal>

#include <array>

namespace imaging {

struct HistogramData {
    static constexpr int kBinCount = 256;

    std::array<quint64, kBinCount> red{};
    std::array<quint64, kBinCount> green{};
    std::array<quint64, kBinCount> blue{};
    quint64 pixelCount = 0;

    [[nodiscard]] bool isEmpty() const noexcept { return pixelCount == 0; }
    [[nodiscard]] quint64 peak() const noexcept;
};

class Histogram final
{
public:
    Histogram() = delete;

    // Calculates an exact 256-bin RGB histogram from every pixel in the supplied
    // display image. No sampling or resizing is performed.
    [[nodiscard]] static HistogramData calculate(const QImage &image);
};

} // namespace imaging
