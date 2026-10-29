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

[[nodiscard]] bool hasManualTransform(const DevelopState &state) noexcept;

// Builds the complete geometry mapping used after RAW development: Crop/Rotate
// orientation and fine straighten first, then the manual Transform controls.
// The returned canvas is intentionally fixed before the manual transform so
// Scale and X/Y Offset retain their Lightroom-style meaning. Safe-crop solving
// is optional because it is substantially more expensive than the homography.
[[nodiscard]] GeometryMapping geometryMappingForState(const QSizeF &sourceSize,
                                                      const DevelopState &state,
                                                      bool computeSafeCrop = false);

} // namespace imaging
