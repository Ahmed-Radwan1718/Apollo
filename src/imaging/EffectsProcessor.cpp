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
#include <numbers>

namespace imaging {
namespace {

struct EffectMapping {
    QTransform transform;
    QRectF cropCanvasRect;
    bool useTransform = false;
};

struct VignetteShape {
    double xScale = 1.0;
    double yScale = 1.0;
    double normPower = 2.0;
    double radiusAdjustment = 0.0;
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

double interpolateThreePoint(double value,
                             double x0,
                             double y0,
                             double x1,
                             double y1,
                             double x2,
                             double y2)
{
    if (value <= x1) {
        const double span = std::max(0.000001, x1 - x0);
        const double t = clamp01((value - x0) / span);
        return y0 + (y1 - y0) * t;
    }

    const double span = std::max(0.000001, x2 - x1);
    const double t = clamp01((value - x1) / span);
    return y1 + (y2 - y1) * t;
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

VignetteShape vignetteShape(int roundness, double aspectRatio)
{
    VignetteShape result;
    const double clampedAspect = std::clamp(aspectRatio, 0.05, 20.0);
    const double round = std::clamp(static_cast<double>(roundness), -100.0, 100.0);

    if (round < 0.0) {
        // Lightroom's negative Roundness moves the post-crop shape toward an
        // almost rectangular superellipse while preserving the crop aspect.
        const double t = -round / 100.0;
        result.normPower = 2.0 + 14.0 * t;
        return result;
    }

    if (round <= 0.0 || std::abs(clampedAspect - 1.0) < 0.000001) {
        return result;
    }

    // Positive Roundness progressively makes the vignette circular in image
    // space instead of merely changing a radial exponent. The ratio progression
    // mirrors Lightroom's observed post-crop geometry and keeps +100 circular.
    const double t = round / 100.0;
    const double ratioParameter = clampedAspect <= 1.0
        ? clampedAspect
        : 2.0 - 1.0 / clampedAspect;
    const double newRatio = ratioParameter
        - (ratioParameter - 1.0) * t;

    if (clampedAspect >= 1.0) {
        result.yScale = 1.0
            / std::max(0.000001,
                       clampedAspect * (2.0 - newRatio));
    } else {
        result.xScale = clampedAspect / std::max(0.000001, newRatio);
    }

    result.radiusAdjustment =
        (1.0 - newRatio / std::max(0.000001, ratioParameter)) * 0.5;
    return result;
}

double vignetteDistance(double u,
                        double v,
                        const VignetteShape &shape)
{
    const double x = std::abs(u - 0.5) * 2.0 * shape.xScale;
    const double y = std::abs(v - 0.5) * 2.0 * shape.yScale;
    const double power = std::max(2.0, shape.normPower);
    return std::pow(std::pow(x, power) + std::pow(y, power),
                    1.0 / power);
}

double vignetteMask(double u,
                    double v,
                    const EffectsSettings &settings,
                    double aspectRatio)
{
    const VignetteShape shape = vignetteShape(settings.vignetteRoundness,
                                              aspectRatio);
    const double distance = vignetteDistance(u, v, shape);

    const double feather = std::clamp(
        static_cast<double>(settings.vignetteFeather), 0.0, 100.0);
    const double midpoint = std::clamp(
        static_cast<double>(settings.vignetteMidpoint), 0.0, 100.0);

    // Lightroom's Feather control is deliberately nonlinear. Around the
    // default value of 50 it creates a very broad transition; Feather 0 is
    // almost a hard edge. Midpoint translates that complete transition rather
    // than simply changing an exponent.
    const double falloffPercent = interpolateThreePoint(
        feather,
        0.0, 5.0,
        50.0, 75.5,
        100.0, 82.0);
    const double centerPercent = interpolateThreePoint(
        feather,
        0.0, 99.5,
        50.0, 92.2,
        100.0, 106.9);

    const double falloff = std::max(0.001, falloffPercent / 100.0);
    double start = centerPercent / 100.0
        + 0.00576 * (midpoint - 50.0)
        - falloff * 0.5;
    start -= shape.radiusAdjustment;

    const double t = clamp01((distance - start) / falloff);
    if (t <= 0.0) {
        return 0.0;
    }
    if (t >= 1.0) {
        return 1.0;
    }

    // Cosine easing matches the characteristic Lightroom post-crop transition
    // much more closely than a generic smoothstep.
    return 0.5 - 0.5 * std::cos(std::numbers::pi_v<double> * t);
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
            const double originalRed = red;
            const double originalGreen = green;
            const double originalBlue = blue;
            const double luminance = clamp01(
                0.2126 * red + 0.7152 * green + 0.0722 * blue);

            if (vignetteStrength != 0.0) {
                const double mask = vignetteMask(u, v, settings, aspectRatio);

                if (vignetteStrength < 0.0) {
                    // Lightroom's negative Amount behaves like a fraction of
                    // the encoded image values. At -100, a fully affected pixel
                    // can therefore reach black rather than stopping at a mild
                    // exposure reduction.
                    const double factor = std::max(
                        0.0,
                        1.0 + vignetteStrength * mask);
                    red *= factor;
                    green *= factor;
                    blue *= factor;

                    if (highlightStrength > 0.0) {
                        // Highlight Priority restores bright local contrast from
                        // the pre-vignette pixel instead of simply weakening the
                        // entire vignette uniformly.
                        const double highlightGate = smoothStep(0.48, 0.98, luminance);
                        const double preserve = clamp01(
                            highlightStrength * highlightGate * (0.35 + 0.65 * mask));
                        red += (originalRed - red) * preserve;
                        green += (originalGreen - green) * preserve;
                        blue += (originalBlue - blue) * preserve;
                    }
                } else {
                    // A screen-style lift retains more local contrast than a
                    // flat white overlay while still allowing +100 to produce
                    // Lightroom-like bright corners.
                    const double lift = clamp01(vignetteStrength * mask);
                    red = red + (1.0 - red) * lift;
                    green = green + (1.0 - green) * lift;
                    blue = blue + (1.0 - blue) * lift;
                }

                red = clamp01(red);
                green = clamp01(green);
                blue = clamp01(blue);
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
