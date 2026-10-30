#pragma once

#include "app/DevelopState.h"

#include <QImage>
#include <QRectF>
#include <QSizeF>

namespace imaging {

enum class CropAspectMode : int {
    Free = 0,
    Original = 1,
    Square = 2,
    Ratio3x2 = 3,
    Ratio4x3 = 4,
    Ratio16x9 = 5,
};

constexpr int kCropCoordinateScale = 100000;

[[nodiscard]] QRectF cropRectFromState(const DevelopState &state);
void setCropRectOnState(DevelopState &state, const QRectF &normalizedRect);

[[nodiscard]] int normalizedQuarterTurns(int quarterTurns) noexcept;
[[nodiscard]] qreal cropRotationDegrees(const DevelopState &state) noexcept;
[[nodiscard]] bool hasCropGeometry(const DevelopState &state) noexcept;

// Returns the largest centered axis-aligned rectangle that contains no empty
// corners after the fine straighten rotation. Coordinates are normalized to
// the final rotated bounding box.
[[nodiscard]] QRectF safeCropRectForStraighten(const QSizeF &sourceSize,
                                               const DevelopState &state);

// Returns the valid no-empty-pixel crop region for the complete geometry stack,
// including perspective, aspect, scale, offsets and manual Transform rotation.
[[nodiscard]] QRectF safeCropRectForGeometry(const QSizeF &sourceSize,
                                             const DevelopState &state);

// Applies Constrain Crop non-destructively without rewriting the saved crop.
[[nodiscard]] QRectF effectiveCropRectFromState(const QSizeF &sourceSize,
                                                const DevelopState &state);

// Applies the full geometry stack after develop rendering, takes the normalized
// crop, then applies Effects in final post-crop coordinates. The input image is
// never changed.
[[nodiscard]] QImage applyCropTransform(const QImage &image,
                                        const DevelopState &state);

} // namespace imaging
