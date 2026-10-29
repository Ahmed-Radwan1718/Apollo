#include "imaging/DetailProcessor.h"

#include <QColorSpace>

#include <algorithm>
#include <cmath>
#include <vector>

namespace imaging {
namespace {

int clampByte(double value)
{
    return std::clamp(static_cast<int>(std::lround(value)), 0, 255);
}

int luminance8(QRgb pixel)
{
    return std::clamp((54 * qRed(pixel)
                       + 183 * qGreen(pixel)
                       + 19 * qBlue(pixel)
                       + 128) >> 8,
                      0,
                      255);
}

QImage makeLuminanceImage(const QImage &image)
{
    QImage result(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *target = result.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            target[x] = static_cast<uchar>(luminance8(source[x]));
        }
    }
    return result;
}

QImage boxBlurGray8(const QImage &source, int radius)
{
    if (source.isNull() || radius <= 0) {
        return source;
    }

    const QImage input = source.format() == QImage::Format_Grayscale8
        ? source
        : source.convertToFormat(QImage::Format_Grayscale8);
    const int width = input.width();
    const int height = input.height();
    if (width <= 0 || height <= 0) {
        return input;
    }

    radius = std::min(radius, std::max(width, height) - 1);
    if (radius <= 0) {
        return input;
    }

    const int diameter = radius * 2 + 1;
    QImage horizontal(input.size(), QImage::Format_Grayscale8);
    QImage output(input.size(), QImage::Format_Grayscale8);

    for (int y = 0; y < height; ++y) {
        const uchar *sourceLine = input.constScanLine(y);
        uchar *targetLine = horizontal.scanLine(y);

        int sum = 0;
        for (int offset = -radius; offset <= radius; ++offset) {
            sum += sourceLine[std::clamp(offset, 0, width - 1)];
        }

        for (int x = 0; x < width; ++x) {
            targetLine[x] = static_cast<uchar>((sum + diameter / 2) / diameter);
            const int removeX = std::clamp(x - radius, 0, width - 1);
            const int addX = std::clamp(x + radius + 1, 0, width - 1);
            sum += static_cast<int>(sourceLine[addX])
                - static_cast<int>(sourceLine[removeX]);
        }
    }

    std::vector<int> columnSums(static_cast<std::size_t>(width), 0);
    for (int offset = -radius; offset <= radius; ++offset) {
        const uchar *line = horizontal.constScanLine(
            std::clamp(offset, 0, height - 1));
        for (int x = 0; x < width; ++x) {
            columnSums[static_cast<std::size_t>(x)] += line[x];
        }
    }

    for (int y = 0; y < height; ++y) {
        uchar *targetLine = output.scanLine(y);
        for (int x = 0; x < width; ++x) {
            targetLine[x] = static_cast<uchar>(
                (columnSums[static_cast<std::size_t>(x)] + diameter / 2) / diameter);
        }

        const int removeY = std::clamp(y - radius, 0, height - 1);
        const int addY = std::clamp(y + radius + 1, 0, height - 1);
        const uchar *removeLine = horizontal.constScanLine(removeY);
        const uchar *addLine = horizontal.constScanLine(addY);
        for (int x = 0; x < width; ++x) {
            columnSums[static_cast<std::size_t>(x)] +=
                static_cast<int>(addLine[x]) - static_cast<int>(removeLine[x]);
        }
    }

    return output;
}

QImage blendGray8(const QImage &left, const QImage &right, double blend)
{
    if (left.isNull()) {
        return right;
    }
    if (right.isNull() || left.size() != right.size()) {
        return left;
    }

    blend = std::clamp(blend, 0.0, 1.0);
    if (blend <= 0.0) {
        return left;
    }
    if (blend >= 1.0) {
        return right;
    }

    QImage result(left.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < left.height(); ++y) {
        const uchar *leftLine = left.constScanLine(y);
        const uchar *rightLine = right.constScanLine(y);
        uchar *targetLine = result.scanLine(y);
        for (int x = 0; x < left.width(); ++x) {
            targetLine[x] = static_cast<uchar>(clampByte(
                static_cast<double>(leftLine[x])
                    + (static_cast<double>(rightLine[x])
                       - static_cast<double>(leftLine[x])) * blend));
        }
    }
    return result;
}

QImage fractionalBlurGray8(const QImage &source, double radius)
{
    radius = std::max(0.0, radius);
    const int lowerRadius = static_cast<int>(std::floor(radius));
    const int upperRadius = static_cast<int>(std::ceil(radius));

    if (upperRadius <= 0) {
        return source;
    }

    const QImage lower = lowerRadius > 0
        ? boxBlurGray8(source, lowerRadius)
        : source;
    if (lowerRadius == upperRadius) {
        return lower;
    }

    const QImage upper = boxBlurGray8(source, upperRadius);
    return blendGray8(lower, upper, radius - static_cast<double>(lowerRadius));
}

double smoothStep(double edge0, double edge1, double value)
{
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0 : 0.0;
    }
    const double t = std::clamp((value - edge0) / (edge1 - edge0), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

void applyColorNoiseReduction(QImage &image, int amount, double radiusScale)
{
    const int clampedAmount = std::clamp(amount, 0, 100);
    if (clampedAmount <= 0 || image.isNull()) {
        return;
    }

    const double strength = static_cast<double>(clampedAmount) / 100.0;
    const int radius = std::clamp(
        static_cast<int>(std::lround((1.0 + 4.0 * strength) * radiusScale)),
        1,
        6);
    const double mix = 0.9 * strength;

    QImage chroma(image.size(), QImage::Format_Grayscale8);
    for (int y = 0; y < image.height(); ++y) {
        const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *target = chroma.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double co = (static_cast<double>(qRed(source[x]))
                               - static_cast<double>(qBlue(source[x]))) * 0.5;
            target[x] = static_cast<uchar>(clampByte(128.0 + co));
        }
    }

    QImage blurred = boxBlurGray8(chroma, radius);
    for (int y = 0; y < image.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *originalChroma = chroma.constScanLine(y);
        const uchar *blurredChroma = blurred.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double deltaCo = 2.0
                * (static_cast<double>(blurredChroma[x])
                   - static_cast<double>(originalChroma[x]))
                * mix;
            const int red = clampByte(qRed(pixels[x]) + deltaCo * 0.5);
            const int blue = clampByte(qBlue(pixels[x]) - deltaCo * 0.5);
            pixels[x] = qRgba(red,
                              qGreen(pixels[x]),
                              blue,
                              qAlpha(pixels[x]));
        }
    }

    for (int y = 0; y < image.height(); ++y) {
        const auto *source = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        uchar *target = chroma.scanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double averageRedBlue =
                (static_cast<double>(qRed(source[x]))
                 + static_cast<double>(qBlue(source[x]))) * 0.5;
            const double cg = (static_cast<double>(qGreen(source[x]))
                               - averageRedBlue) * 0.5;
            target[x] = static_cast<uchar>(clampByte(128.0 + cg));
        }
    }

    blurred = boxBlurGray8(chroma, radius);
    for (int y = 0; y < image.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *originalChroma = chroma.constScanLine(y);
        const uchar *blurredChroma = blurred.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double deltaCg = 2.0
                * (static_cast<double>(blurredChroma[x])
                   - static_cast<double>(originalChroma[x]))
                * mix;
            pixels[x] = qRgba(qRed(pixels[x]),
                              clampByte(qGreen(pixels[x]) + deltaCg),
                              qBlue(pixels[x]),
                              qAlpha(pixels[x]));
        }
    }
}

void applyLuminanceNoiseReduction(QImage &image, int amount, double radiusScale)
{
    const int clampedAmount = std::clamp(amount, 0, 100);
    if (clampedAmount <= 0 || image.isNull()) {
        return;
    }

    const double strength = static_cast<double>(clampedAmount) / 100.0;
    const int radius = std::clamp(
        static_cast<int>(std::lround((1.0 + 3.0 * strength) * radiusScale)),
        1,
        5);
    const double mix = 0.88 * strength;
    const double edgeSigma = 5.0 + 20.0 * strength;

    const QImage luminance = makeLuminanceImage(image);
    const QImage blurred = boxBlurGray8(luminance, radius);

    for (int y = 0; y < image.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *sourceLuma = luminance.constScanLine(y);
        const uchar *blurredLuma = blurred.constScanLine(y);
        for (int x = 0; x < image.width(); ++x) {
            const double difference = static_cast<double>(blurredLuma[x])
                - static_cast<double>(sourceLuma[x]);
            const double normalized = std::abs(difference) / edgeSigma;
            const double edgeWeight = 1.0 / (1.0 + normalized * normalized);
            const double delta = difference * mix * edgeWeight;

            pixels[x] = qRgba(clampByte(qRed(pixels[x]) + delta),
                              clampByte(qGreen(pixels[x]) + delta),
                              clampByte(qBlue(pixels[x]) + delta),
                              qAlpha(pixels[x]));
        }
    }
}

void applySharpening(QImage &image,
                     const DetailSettings &settings,
                     double radiusScale)
{
    const int amount = std::clamp(settings.sharpeningAmount, 0, 150);
    if (amount <= 0 || image.isNull()) {
        return;
    }

    const int detail = std::clamp(settings.sharpeningDetail, 0, 100);
    const int masking = std::clamp(settings.sharpeningMasking, 0, 100);
    const double requestedRadius = std::clamp(
        static_cast<double>(settings.sharpeningRadiusHundredths) / 100.0,
        0.5,
        3.0);
    const double effectiveRadius = std::clamp(requestedRadius * radiusScale,
                                              0.15,
                                              3.0);
    const double fineRadius = std::max(0.15, effectiveRadius * 0.35);

    const QImage luminance = makeLuminanceImage(image);
    const QImage coarseBlur = fractionalBlurGray8(luminance, effectiveRadius);
    const QImage fineBlur = fractionalBlurGray8(luminance, fineRadius);

    const double detailMix = static_cast<double>(detail) / 100.0;
    const double amountGain = static_cast<double>(amount) / 100.0 * 1.1;
    const double maskingThreshold = static_cast<double>(masking) * 0.65;
    const double maskingSoftness = 12.0 + maskingThreshold * 0.35;

    for (int y = 0; y < image.height(); ++y) {
        auto *pixels = reinterpret_cast<QRgb *>(image.scanLine(y));
        const uchar *lumaLine = luminance.constScanLine(y);
        const uchar *coarseLine = coarseBlur.constScanLine(y);
        const uchar *fineLine = fineBlur.constScanLine(y);
        const uchar *upperLine = luminance.constScanLine(std::max(0, y - 1));
        const uchar *lowerLine = luminance.constScanLine(
            std::min(image.height() - 1, y + 1));

        for (int x = 0; x < image.width(); ++x) {
            const int leftX = std::max(0, x - 1);
            const int rightX = std::min(image.width() - 1, x + 1);
            const double gradient =
                std::abs(static_cast<double>(lumaLine[rightX])
                         - static_cast<double>(lumaLine[leftX]))
                + std::abs(static_cast<double>(lowerLine[x])
                           - static_cast<double>(upperLine[x]));
            const double edgeMask = masking <= 0
                ? 1.0
                : smoothStep(maskingThreshold,
                             maskingThreshold + maskingSoftness,
                             gradient);

            const double sourceLuma = static_cast<double>(lumaLine[x]);
            const double coarseBand = sourceLuma - static_cast<double>(coarseLine[x]);
            const double fineBand = sourceLuma - static_cast<double>(fineLine[x]);
            const double band = coarseBand * (1.0 - 0.75 * detailMix)
                + fineBand * (1.25 * detailMix);
            const double delta = std::clamp(amountGain * band * edgeMask,
                                            -48.0,
                                            48.0);

            pixels[x] = qRgba(clampByte(qRed(pixels[x]) + delta),
                              clampByte(qGreen(pixels[x]) + delta),
                              clampByte(qBlue(pixels[x]) + delta),
                              qAlpha(pixels[x]));
        }
    }
}

} // namespace

QImage DetailProcessor::apply(const QImage &image,
                              const DetailSettings &settings,
                              double radiusScale)
{
    if (image.isNull()) {
        return image;
    }

    const bool hasVisibleAdjustment = settings.sharpeningAmount > 0
        || settings.luminanceNoiseReduction > 0
        || settings.colorNoiseReduction > 0;
    if (!hasVisibleAdjustment) {
        return image;
    }

    radiusScale = std::clamp(radiusScale, 0.25, 1.0);
    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    result.setDevicePixelRatio(image.devicePixelRatio());
    result.setColorSpace(image.colorSpace());

    applyColorNoiseReduction(result,
                             settings.colorNoiseReduction,
                             radiusScale);
    applyLuminanceNoiseReduction(result,
                                 settings.luminanceNoiseReduction,
                                 radiusScale);
    applySharpening(result, settings, radiusScale);
    return result;
}

} // namespace imaging
