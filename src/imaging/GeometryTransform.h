#pragma once

#include "app/DevelopState.h"

#include <QPolygonF>
#include <QRectF>
#include <QSizeF>
#include <QTransform>

namespace imaging {

struct GeometryMapping {
    bool valid = false;
    QTransform transform;
    QRectF canvasRect;
    QPolygonF contentPolygon;
    QRectF safeNormalizedRect{0.0, 0.0, 1.0, 1.0};
};

// Kept for compatibility with the existing crop pipeline. This returns true
// when either the analyzed Upright stage or the manual Transform stage changes
// the geometry.
[[nodiscard]] bool hasManualTransform(const DevelopState &state) noexcept;

// Builds the complete geometry mapping used after RAW development: Crop/Rotate
// orientation and fine straighten first, then the analyzed Upright correction,
// then the manual Transform controls. The canvas is intentionally fixed before
// both stages so Scale and X/Y Offset retain their Lightroom-style meaning.
// Safe-crop solving is optional because it is substantially more expensive than
// the homography.
[[nodiscard]] GeometryMapping geometryMappingForState(const QSizeF &sourceSize,
                                                      const DevelopState &state,
                                                      bool computeSafeCrop = false);

} // namespace imaging
