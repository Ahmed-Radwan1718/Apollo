#include "imaging/CropTransform.h"

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace imaging {
namespace {

constexpr qreal kMinimumCropExtent = 1.0 / static_cast<qreal>(kCropCoordinateScale);
constexpr qreal kEpsilon = 1.0e-9;

QRectF clampedNormalizedRect(QRectF rect)
{
    if (!rect.isValid() || rect.isEmpty()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    rect = rect.normalized();
    const qreal width = std::clamp(rect.width(), kMinimumCropExtent, 1.0);
    const qreal height = std::clamp(rect.height(), kMinimumCropExtent, 1.0);
    const qreal x = std::clamp(rect.x(), 0.0, 1.0 - width);
    const qreal y = std::clamp(rect.y(), 0.0, 1.0 - height);
    return QRectF(x, y, width, height);
}

} // namespace

QRectF cropRectFromState(const DevelopState &state)
{
    const qreal scale = static_cast<qreal>(kCropCoordinateScale);
    return clampedNormalizedRect(QRectF(
        static_cast<qreal>(state.cropX) / scale,
        static_cast<qreal>(state.cropY) / scale,
        static_cast<qreal>(state.cropWidth) / scale,
        static_cast<qreal>(state.cropHeight) / scale));
}

void setCropRectOnState(DevelopState &state, const QRectF &normalizedRect)
{
    const QRectF rect = clampedNormalizedRect(normalizedRect);
    const qreal scale = static_cast<qreal>(kCropCoordinateScale);

    state.cropX = std::clamp(static_cast<int>(std::lround(rect.x() * scale)),
                             0, kCropCoordinateScale - 1);
    state.cropY = std::clamp(static_cast<int>(std::lround(rect.y() * scale)),
                             0, kCropCoordinateScale - 1);
    state.cropWidth = std::clamp(static_cast<int>(std::lround(rect.width() * scale)),
                                 1, kCropCoordinateScale - state.cropX);
    state.cropHeight = std::clamp(static_cast<int>(std::lround(rect.height() * scale)),
                                  1, kCropCoordinateScale - state.cropY);
}

int normalizedQuarterTurns(int quarterTurns) noexcept
{
    int normalized = quarterTurns % 4;
    if (normalized < 0) {
        normalized += 4;
    }
    return normalized;
}

qreal cropRotationDegrees(const DevelopState &state) noexcept
{
    return static_cast<qreal>(normalizedQuarterTurns(state.cropQuarterTurns)) * 90.0
        + static_cast<qreal>(state.cropStraightenHundredths) / 100.0;
}

bool hasCropGeometry(const DevelopState &state) noexcept
{
    return state.cropX != 0
        || state.cropY != 0
        || state.cropWidth != kCropCoordinateScale
        || state.cropHeight != kCropCoordinateScale
        || normalizedQuarterTurns(state.cropQuarterTurns) != 0
        || state.cropStraightenHundredths != 0;
}

QRectF safeCropRectForStraighten(const QSizeF &sourceSize,
                                 const DevelopState &state)
{
    if (!sourceSize.isValid() || sourceSize.isEmpty()) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    qreal width = sourceSize.width();
    qreal height = sourceSize.height();
    if (normalizedQuarterTurns(state.cropQuarterTurns) % 2 != 0) {
        std::swap(width, height);
    }

    const qreal degrees = std::clamp(
        std::abs(static_cast<qreal>(state.cropStraightenHundredths) / 100.0),
        0.0,
        45.0);
    if (degrees <= kEpsilon) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    const qreal radians = degrees * std::numbers::pi_v<qreal> / 180.0;
    const qreal sine = std::abs(std::sin(radians));
    const qreal cosine = std::abs(std::cos(radians));
    const qreal boundingWidth = width * cosine + height * sine;
    const qreal boundingHeight = width * sine + height * cosine;

    if (boundingWidth <= kEpsilon || boundingHeight <= kEpsilon) {
        return QRectF(0.0, 0.0, 1.0, 1.0);
    }

    qreal safeWidth = width;
    qreal safeHeight = height;
    const bool widthIsLonger = width >= height;
    const qreal longSide = std::max(width, height);
    const qreal shortSide = std::min(width, height);

    if (shortSide <= 2.0 * sine * cosine * longSide
        || std::abs(sine - cosine) <= kEpsilon) {
        const qreal halfShort = 0.5 * shortSide;
        if (widthIsLonger) {
            safeWidth = halfShort / sine;
            safeHeight = halfShort / cosine;
        } else {
            safeWidth = halfShort / cosine;
            safeHeight = halfShort / sine;
        }
    } else {
        const qreal cosineDoubleAngle = cosine * cosine - sine * sine;
        if (std::abs(cosineDoubleAngle) > kEpsilon) {
            safeWidth = (width * cosine - height * sine) / cosineDoubleAngle;
            safeHeight = (height * cosine - width * sine) / cosineDoubleAngle;
        }
    }

    const qreal normalizedWidth = std::clamp(safeWidth / boundingWidth, 0.0, 1.0);
    const qreal normalizedHeight = std::clamp(safeHeight / boundingHeight, 0.0, 1.0);
    return QRectF((1.0 - normalizedWidth) * 0.5,
                  (1.0 - normalizedHeight) * 0.5,
                  normalizedWidth,
                  normalizedHeight);
}

QImage applyCropTransform(const QImage &image, const DevelopState &state)
{
    if (image.isNull() || !hasCropGeometry(state)) {
        return image;
    }

    QImage transformed = image;
    const qreal rotation = cropRotationDegrees(state);
    if (std::abs(rotation) > kEpsilon) {
        QTransform transform;
        transform.rotate(rotation);
        transformed = image.transformed(transform, Qt::SmoothTransformation);
        transformed.setDevicePixelRatio(1.0);
    }

    if (transformed.isNull()) {
        return transformed;
    }

    const QRectF normalized = cropRectFromState(state);
    QRect pixelRect(
        static_cast<int>(std::floor(normalized.left() * transformed.width())),
        static_cast<int>(std::floor(normalized.top() * transformed.height())),
        std::max(1, static_cast<int>(std::ceil(normalized.width() * transformed.width()))),
        std::max(1, static_cast<int>(std::ceil(normalized.height() * transformed.height()))));
    pixelRect = pixelRect.intersected(transformed.rect());

    if (pixelRect.isEmpty()) {
        return transformed;
    }

    QImage cropped = transformed.copy(pixelRect);
    cropped.setDevicePixelRatio(1.0);
    return cropped;
}

} // namespace imaging
