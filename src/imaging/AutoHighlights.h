#pragma once

#include "imaging/SrgbTransfer.h"

#include <QImage>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

namespace imaging {

struct AutoHighlightsResult
{
    bool valid = false;
    qreal deltaValue = 0.0;
    qreal confidence = 0.0;
};

namespace detail {

constexpr int kAutoHighlightsMaxDimension = 900;
constexpr int kAutoHighlightsHistogramBins = 4096;
constexpr int kAutoHighlightsMinimumPixels = 1024;
constexpr qreal kSceneMiddleGray = 0.18;
constexpr qreal kHighlightMaskStartEv = 0.5;
constexpr qreal kHighlightMaskFullEv = 2.0;
constexpr qreal kMaximumHighlightSlopeChange = 0.9;
constexpr qreal kEpsilon = 1.0e-5;

template <typename Histogram>
inline qreal percentile(const Histogram &histogram, int pixelCount, qreal fraction)
{
    if (pixelCount <= 0) {
        return 0.0;
    }

    const int target = std::clamp(
        static_cast<int>(std::ceil(std::clamp(fraction, 0.0, 1.0) * pixelCount)),
        1,
        pixelCount);
    int cumulative = 0;
    for (int index = 0; index < kAutoHighlightsHistogramBins; ++index) {
        cumulative += histogram[index];
        if (cumulative >= target) {
            return (static_cast<qreal>(index) + 0.5)
                / static_cast<qreal>(kAutoHighlightsHistogramBins);
        }
    }

    return 1.0;
}

inline qreal integratedHighlightWeightEv(qreal sceneEv)
{
    if (sceneEv <= kHighlightMaskStartEv) {
        return 0.0;
    }

    const qreal transitionWidth = kHighlightMaskFullEv - kHighlightMaskStartEv;
    if (sceneEv >= kHighlightMaskFullEv) {
        // Integral of smoothstep(0..1) is 0.5. Above the transition, the
        // highlight mask is fully active so the integral continues linearly.
        return 0.5 * transitionWidth + (sceneEv - kHighlightMaskFullEv);
    }

    const qreal normalized =
        (sceneEv - kHighlightMaskStartEv) / transitionWidth;
    const qreal integratedSmoothstep =
        normalized * normalized * normalized
        - 0.5 * normalized * normalized * normalized * normalized;
    return transitionWidth * integratedSmoothstep;
}

inline qreal sliderSuggestion(qreal luminance, qreal targetLuminance)
{
    luminance = std::clamp(luminance, kEpsilon, 1.0);
    targetLuminance = std::clamp(targetLuminance, kEpsilon, 1.0);

    const qreal sceneEv = std::log2(luminance / kSceneMiddleGray);
    const qreal weightedEv = integratedHighlightWeightEv(sceneEv);
    if (weightedEv < 0.06) {
        return 0.0;
    }

    const qreal requiredEv = std::log2(targetLuminance / luminance);
    const qreal fullScaleResponse = kMaximumHighlightSlopeChange * weightedEv;
    if (std::abs(fullScaleResponse) <= kEpsilon) {
        return 0.0;
    }

    return std::clamp(requiredEv / fullScaleResponse * 100.0, -100.0, 100.0);
}

} // namespace detail

inline AutoHighlightsResult estimateAutoHighlights(const QImage &source)
{
    using namespace detail;

    AutoHighlightsResult result;
    if (source.isNull()) {
        return result;
    }

    QImage image = source;
    const int maximumDimension = std::max(image.width(), image.height());
    if (maximumDimension > kAutoHighlightsMaxDimension) {
        image = image.scaled(kAutoHighlightsMaxDimension,
                             kAutoHighlightsMaxDimension,
                             Qt::KeepAspectRatio,
                             Qt::SmoothTransformation);
    }
    image = image.convertToFormat(QImage::Format_RGB32);

    if (image.width() <= 0 || image.height() <= 0) {
        return result;
    }

    std::array<int, kAutoHighlightsHistogramBins> histogram{};
    int pixelCount = 0;
    int nearClipCount = 0;
    int clippedChannelCount = 0;

    for (int y = 0; y < image.height(); ++y) {
        const QRgb *row = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < image.width(); ++x) {
            const QRgb pixel = row[x];
            const int red8 = qRed(pixel);
            const int green8 = qGreen(pixel);
            const int blue8 = qBlue(pixel);

            const qreal red = srgbToLinear(red8);
            const qreal green = srgbToLinear(green8);
            const qreal blue = srgbToLinear(blue8);
            const qreal luminance = std::clamp(
                0.2126 * red + 0.7152 * green + 0.0722 * blue,
                0.0,
                1.0);

            const int bin = std::clamp(
                static_cast<int>(std::floor(
                    luminance * static_cast<qreal>(kAutoHighlightsHistogramBins - 1))),
                0,
                kAutoHighlightsHistogramBins - 1);
            ++histogram[bin];
            ++pixelCount;

            const int maximumChannel = std::max({red8, green8, blue8});
            if (maximumChannel >= 252) {
                ++nearClipCount;
            }
            if (maximumChannel >= 254) {
                ++clippedChannelCount;
            }
        }
    }

    if (pixelCount < kAutoHighlightsMinimumPixels) {
        return result;
    }

    const qreal p90 = percentile(histogram, pixelCount, 0.90);
    const qreal p97 = percentile(histogram, pixelCount, 0.97);
    const qreal p995 = percentile(histogram, pixelCount, 0.995);

    // If even the extreme upper tail never reaches Apollo's highlight region,
    // Highlights is not the correct control to use. Leave the image alone.
    if (p995 < 0.28) {
        result.valid = true;
        result.deltaValue = 0.0;
        result.confidence = 0.25;
        return result;
    }

    // Targets deliberately leave headroom for speculars. They are evaluated at
    // several upper percentiles so a tiny bright light cannot dominate the
    // recommendation while broad blown highlights are still recovered strongly.
    const qreal suggestion90 = sliderSuggestion(p90, 0.55);
    const qreal suggestion97 = sliderSuggestion(p97, 0.75);
    const qreal suggestion995 = sliderSuggestion(p995, 0.90);

    qreal delta = 0.20 * suggestion90
        + 0.50 * suggestion97
        + 0.30 * suggestion995;

    const qreal nearClipFraction =
        static_cast<qreal>(nearClipCount) / static_cast<qreal>(pixelCount);
    const qreal clippedFraction =
        static_cast<qreal>(clippedChannelCount) / static_cast<qreal>(pixelCount);

    if (nearClipFraction > 0.01) {
        const qreal severity = std::clamp((nearClipFraction - 0.01) / 0.08, 0.0, 1.0);
        delta -= 10.0 * severity;
    }
    if (clippedFraction > 0.001) {
        const qreal severity = std::clamp((clippedFraction - 0.001) / 0.025, 0.0, 1.0);
        delta -= 28.0 * severity;
    }

    // Never brighten highlights while meaningful channel clipping is already
    // present. In that case recovery is the only safe automatic direction.
    if (clippedFraction > 0.0005) {
        delta = std::min<qreal>(delta, 0.0);
    }

    delta = std::clamp(delta, -100.0, 65.0);
    if (std::abs(delta) < 1.0) {
        delta = 0.0;
    }

    const qreal highlightSpan = std::max<qreal>(0.0, p995 - p90);
    const qreal highlightPresence = std::clamp((p97 - 0.28) / 0.45, 0.0, 1.0);

    result.valid = true;
    result.deltaValue = delta;
    result.confidence = std::clamp(
        0.30 + 0.45 * highlightPresence + 0.25 * std::clamp(highlightSpan / 0.35, 0.0, 1.0),
        0.0,
        1.0);
    return result;
}

} // namespace imaging
