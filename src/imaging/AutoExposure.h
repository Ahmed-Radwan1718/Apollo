#pragma once

#include <QImage>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace imaging {

struct AutoExposureResult
{
    bool valid = false;
    qreal deltaStops = 0.0;
    qreal confidence = 0.0;
};

namespace detail {

constexpr int kAutoExposureMaxDimension = 900;
constexpr int kAutoExposureHistogramBins = 4096;
constexpr int kAutoExposureMinimumPixels = 1024;
constexpr qreal kAutoExposureTargetMiddle = 0.18;
constexpr qreal kAutoExposureHighlightTarget = 0.995;
constexpr qreal kAutoExposureHighlightAllowanceStops = 0.10;
constexpr qreal kAutoExposureShadowFloor = 0.004;
constexpr qreal kAutoExposureShadowAllowanceStops = 0.10;
constexpr qreal kAutoExposureEpsilon = 1.0e-5;

inline qreal srgbToLinear(int channel)
{
    const qreal value = static_cast<qreal>(channel) / 255.0;
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
}

template <typename Histogram>
inline qreal percentile(const Histogram &histogram, qreal totalWeight, qreal fraction)
{
    if (totalWeight <= 0.0) {
        return 0.0;
    }

    const qreal target = std::clamp(fraction, 0.0, 1.0) * totalWeight;
    qreal cumulative = 0.0;
    for (int index = 0; index < kAutoExposureHistogramBins; ++index) {
        cumulative += static_cast<qreal>(histogram[index]);
        if (cumulative >= target) {
            return (static_cast<qreal>(index) + 0.5)
                / static_cast<qreal>(kAutoExposureHistogramBins);
        }
    }

    return 1.0;
}

inline qreal clamp01(qreal value)
{
    return std::clamp(value, 0.0, 1.0);
}

} // namespace detail

inline AutoExposureResult estimateAutoExposure(const QImage &source)
{
    using namespace detail;

    AutoExposureResult result;
    if (source.isNull()) {
        return result;
    }

    QImage image = source;
    const int maximumDimension = std::max(image.width(), image.height());
    if (maximumDimension > kAutoExposureMaxDimension) {
        image = image.scaled(kAutoExposureMaxDimension,
                             kAutoExposureMaxDimension,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGB32);

    if (image.width() <= 0 || image.height() <= 0) {
        return result;
    }

    std::array<qreal, kAutoExposureHistogramBins> weightedHistogram{};
    std::array<int, kAutoExposureHistogramBins> globalHistogram{};

    qreal totalWeight = 0.0;
    int pixelCount = 0;

    const qreal halfWidth = std::max<qreal>(1.0, image.width() * 0.5);
    const qreal halfHeight = std::max<qreal>(1.0, image.height() * 0.5);

    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        const qreal normalizedY =
            (static_cast<qreal>(y) + 0.5 - halfHeight) / halfHeight;

        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = row[x];
            const qreal red = srgbToLinear(qRed(pixel));
            const qreal green = srgbToLinear(qGreen(pixel));
            const qreal blue = srgbToLinear(qBlue(pixel));
            const qreal luminance = clamp01(
                0.2126 * red + 0.7152 * green + 0.0722 * blue);

            const int bin = std::clamp(
                static_cast<int>(std::floor(
                    luminance * static_cast<qreal>(kAutoExposureHistogramBins - 1))),
                0,
                kAutoExposureHistogramBins - 1);

            const qreal normalizedX =
                (static_cast<qreal>(x) + 0.5 - halfWidth) / halfWidth;
            const qreal radiusSquared =
                normalizedX * normalizedX + normalizedY * normalizedY;
            const qreal centerWeight =
                1.0 + 1.25 * (1.0 - std::clamp(radiusSquared, 0.0, 1.0));

            weightedHistogram[bin] += centerWeight;
            globalHistogram[bin] += 1;
            totalWeight += centerWeight;
            ++pixelCount;
        }
    }

    if (pixelCount < kAutoExposureMinimumPixels || totalWeight <= 0.0) {
        return result;
    }

    const qreal low = percentile(weightedHistogram, totalWeight, 0.03);
    const qreal lowerMiddle = percentile(weightedHistogram, totalWeight, 0.25);
    const qreal upperMiddle = percentile(weightedHistogram, totalWeight, 0.75);
    const qreal highlight =
        percentile(globalHistogram, static_cast<qreal>(pixelCount), 0.99);

    const qreal robustMiddle = std::sqrt(
        std::max(lowerMiddle, kAutoExposureEpsilon)
        * std::max(upperMiddle, kAutoExposureEpsilon));

    if (!std::isfinite(robustMiddle) || robustMiddle <= 0.0) {
        return result;
    }

    qreal correctionStops =
        std::log2(kAutoExposureTargetMiddle / robustMiddle);

    if (correctionStops > 0.0) {
        const qreal highlightLimit =
            std::log2(kAutoExposureHighlightTarget
                      / std::max(highlight, kAutoExposureEpsilon))
            + kAutoExposureHighlightAllowanceStops;
        correctionStops = std::min(correctionStops, highlightLimit);
    } else if (correctionStops < 0.0) {
        const qreal shadowLimit =
            std::log2(kAutoExposureShadowFloor
                      / std::max(low, kAutoExposureEpsilon))
            - kAutoExposureShadowAllowanceStops;
        correctionStops = std::max(correctionStops, shadowLimit);
    }

    correctionStops = std::clamp(correctionStops, -5.0, 5.0);
    if (!std::isfinite(correctionStops)) {
        return result;
    }

    const qreal tonalSpread = std::max<qreal>(0.0, highlight - low);
    result.valid = true;
    result.deltaStops = std::abs(correctionStops) < 0.005
        ? 0.0
        : correctionStops;
    result.confidence = std::clamp(tonalSpread / 0.35, 0.20, 1.0);
    return result;
}

} // namespace imaging
