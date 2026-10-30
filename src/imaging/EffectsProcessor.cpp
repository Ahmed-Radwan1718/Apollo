#include "imaging/EffectsProcessor.h"

#include "imaging/CropTransform.h"
#include "imaging/GeometryTransform.h"

#include <QColorSpace>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace imaging {
namespace {

struct EffectMapping {
    QTransform transform;
    QRectF cropCanvasRect;
    bool useTransform = false;
};

double clamp01(double value)
{
    return std::clamp(value, 0.0, 1.0);
}

double smoothStep(double edge0, double edge1, double value)
{
    if (edge1 <= edge0) {
        return value >= edge1 ? 1.0 : 0.0;
    }

    const double t = clamp01((value - edge0) / (edge1 - edge0));
    return t * t * (3.0 - 2.0 * t);
}

std::uint32_t mixBits(std::uint32_t value)
{
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return value;
}

double latticeNoise(int x, int y, std::uint32_t seed)
{
    const std::uint32_t ux = static_cast<std::uint32_t>(x);
    const std::uint32_t uy = static_cast<std::uint32_t>(y);
    const std::uint32_t hash = mixBits(seed
        ^ mixBits(ux + 0x9e3779b9U)
        ^ mixBits(uy + 0x85ebca6bU));
    return static_cast<double>(hash & 0x00ffffffU)
        / static_cast<double>(0x007fffffU) - 1.0;
}

double valueNoise(double x, double y, std::uint32_t seed)
{
    const int x0 = static_cast<int>(std::floor(x));
    const int y0 = static_cast<int>(std::floor(y));
    const int x1 = x0 + 1;
    const int y1 = y0 + 1;

    const double tx = x - static_cast<double>(x0);
    const double ty = y - static_cast<double>(y0);
    const double sx = tx * tx * (3.0 - 2.0 * tx);
    const double sy = ty * ty * (3.0 - 2.0 * ty);

    const double n00 = latticeNoise(x0, y0, seed);
    const double n10 = latticeNoise(x1, y0, seed);
    const double n01 = latticeNoise(x0, y1, seed);
    const double n11 = latticeNoise(x1, y1, seed);

    const double top = n00 + (n10 - n00) * sx;
    const double bottom = n01 + (n11 - n01) * sx;
    return top + (bottom - top) * sy;
}

double grainNoise(double x,
                  double y,
                  int size,
                  int roughness)
{
    const double sizeScale = 0.75 + 4.25 * clamp01(static_cast<double>(size) / 100.0);
    const double rough = clamp01(static_cast<double>(roughness) / 100.0);

    const double base = valueNoise(x / sizeScale,
                                   y / sizeScale,
                                   0xa11010U);
    const double coarse = valueNoise(x / (sizeScale * 1.9),
                                     y / (sizeScale * 1.9),
                                     0x6d2b79f5U);
    const double fine = valueNoise(x / std::max(0.35, sizeScale * 0.45),
                                   y / std::max(0.35, sizeScale * 0.45),
                                   0x1b873593U);

    const double smoothGrain = 0.78 * base + 0.22 * coarse;
    const double roughGrain = 0.56 * base + 0.44 * fine;
    return smoothGrain + (roughGrain - smoothGrain) * rough;
}

double vignetteDistance(double u,
                        double v,
                        int roundness,
                        double aspectRatio)
{
    const double dx = std::abs(u - 0.5) * 2.0;
    const double dy = std::abs(v - 0.5) * 2.0;
    const double ellipse = std::sqrt(dx * dx + dy * dy);
    const double box = std::max(dx, dy);

    double circle = ellipse;
    if (aspectRatio > 1.0) {
        const double scaledX = dx * aspectRatio;
        circle = std::sqrt(scaledX * scaledX + dy * dy);
    } else if (aspectRatio > 0.0 && aspectRatio < 1.0) {
        const double scaledY = dy / aspectRatio;
        circle = std::sqrt(dx * dx + scaledY * scaledY);
    }

    const double amount = clamp01(std::abs(static_cast<double>(roundness)) / 100.0);
    if (roundness < 0) {
        return ellipse + (box - ellipse) * amount;
    }
    return ellipse + (circle - ellipse) * amount;
}

double vignetteMask(double u,
                    double v,
                    const EffectsSettings &settings,
                    double aspectRatio)
{
    const double distance = vignetteDistance(u,
                                             v,
                                             settings.vignetteRoundness,
                                             aspectRatio);
    const double midpoint = clamp01(
        static_cast<double>(settings.vignetteMidpoint) / 100.0);
    const double innerRadius = 0.10 + 0.70 * midpoint;
    const double normalized = clamp01(
        (distance - innerRadius) / std::max(0.001, 1.0 - innerRadius));
    const double eased = normalized * normalized * (3.0 - 2.0 * normalized);

    const double feather = clamp01(
        static_cast<double>(settings.vignetteFeather) / 100.0);
    const double exponent = 1.0 + (1.0 - feather) * 5.0;
    return std::pow(eased, exponent);
}

EffectMapping mappingForState(const QImage &image, const DevelopState &state)
{
    EffectMapping result;
    if (image.isNull()) {
        return result;
    }

    const GeometryMapping geometry = geometryMappingForState(QSizeF(image.size()), state);
    if (!geometry.valid || geometry.canvasRect.isEmpty()) {
        result.cropCanvasRect = QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
        return result;
    }

    const QRectF crop = effectiveCropRectFromState(QSizeF(image.size()), state);
    result.transform = geometry.transform;
    result.cropCanvasRect = QRectF(
        geometry.canvasRect.left() + crop.left() * geometry.canvasRect.width(),
        geometry.canvasRect.top() + crop.top() * geometry.canvasRect.height(),
        crop.width() * geometry.canvasRect.width(),
        crop.height() * geometry.canvasRect.height());
    result.useTransform = true;
    return result;
}

QImage applyMapped(const QImage &image,
                   const EffectsSettings &settings,
                   double pixelScale,
                   const EffectMapping &mapping)
{
    if (image.isNull() || !settings.hasVisibleEffects()) {
        return image;
    }

    const double scale = std::clamp(pixelScale, 0.01, 4.0);
    QRectF cropRect = mapping.cropCanvasRect;
    if (!cropRect.isValid() || cropRect.isEmpty()) {
        cropRect = QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
    }

    const double aspectRatio = cropRect.height() > 0.0
        ? cropRect.width() / cropRect.height()
        : 1.0;

    QImage result = image.convertToFormat(QImage::Format_ARGB32);
    result.setDevicePixelRatio(image.devicePixelRatio());
    if (image.colorSpace().isValid()) {
        result.setColorSpace(image.colorSpace());
    }

    const double vignetteStrength =
        std::clamp(static_cast<double>(settings.vignetteAmount) / 100.0,
                   -1.0,
                   1.0);
    const double grainStrength = clamp01(
        static_cast<double>(settings.grainAmount) / 100.0);
    const double highlightStrength = clamp01(
        static_cast<double>(settings.vignetteHighlights) / 100.0);

    for (int y = 0; y < result.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb *>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            const QRgb pixel = line[x];
            if (qAlpha(pixel) == 0) {
                continue;
            }

            QPointF finalPoint(static_cast<double>(x) + 0.5,
                               static_cast<double>(y) + 0.5);
            if (mapping.useTransform) {
                finalPoint = mapping.transform.map(finalPoint);
            }

            const double u = (finalPoint.x() - cropRect.left()) / cropRect.width();
            const double v = (finalPoint.y() - cropRect.top()) / cropRect.height();
            if (u < 0.0 || u > 1.0 || v < 0.0 || v > 1.0) {
                continue;
            }

            double red = static_cast<double>(qRed(pixel)) / 255.0;
            double green = static_cast<double>(qGreen(pixel)) / 255.0;
            double blue = static_cast<double>(qBlue(pixel)) / 255.0;
            const double luminance = clamp01(
                0.2126 * red + 0.7152 * green + 0.0722 * blue);

            if (vignetteStrength != 0.0) {
                const double mask = vignetteMask(u, v, settings, aspectRatio);
                double factor = std::exp2(vignetteStrength * 1.5 * mask);
                if (vignetteStrength < 0.0 && highlightStrength > 0.0) {
                    const double bright = smoothStep(0.55, 0.95, luminance);
                    const double protect = highlightStrength * bright;
                    factor = 1.0 + (factor - 1.0) * (1.0 - protect);
                }

                red = clamp01(red * factor);
                green = clamp01(green * factor);
                blue = clamp01(blue * factor);
            }

            if (grainStrength > 0.0) {
                const double fullResolutionX =
                    (finalPoint.x() - cropRect.left()) / scale;
                const double fullResolutionY =
                    (finalPoint.y() - cropRect.top()) / scale;
                const double noise = grainNoise(fullResolutionX,
                                                fullResolutionY,
                                                settings.grainSize,
                                                settings.grainRoughness);
                const double adjustedLuminance = clamp01(
                    0.2126 * red + 0.7152 * green + 0.0722 * blue);
                const double midtoneWeight =
                    0.58 + 0.42 * (1.0 - std::abs(2.0 * adjustedLuminance - 1.0));
                const double rough = clamp01(
                    static_cast<double>(settings.grainRoughness) / 100.0);
                const double amplitude = grainStrength * (0.08 + 0.06 * rough)
                    * midtoneWeight;
                const double grain = noise * amplitude;
                red = clamp01(red + grain);
                green = clamp01(green + grain);
                blue = clamp01(blue + grain);
            }

            line[x] = qRgba(
                std::clamp(static_cast<int>(std::lround(red * 255.0)), 0, 255),
                std::clamp(static_cast<int>(std::lround(green * 255.0)), 0, 255),
                std::clamp(static_cast<int>(std::lround(blue * 255.0)), 0, 255),
                qAlpha(pixel));
        }
    }

    return result;
}

} // namespace

QImage EffectsProcessor::apply(const QImage &image,
                               const EffectsSettings &settings,
                               double pixelScale)
{
    EffectMapping mapping;
    mapping.cropCanvasRect = QRectF(QPointF(0.0, 0.0), QSizeF(image.size()));
    return applyMapped(image, settings, pixelScale, mapping);
}

QImage EffectsProcessor::applyForCanvas(const QImage &image,
                                        const DevelopState &state,
                                        double pixelScale)
{
    return applyMapped(image,
                       state.effects,
                       pixelScale,
                       mappingForState(image, state));
}

} // namespace imaging
