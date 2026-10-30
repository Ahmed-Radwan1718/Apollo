#pragma once

#include "app/DevelopState.h"
#include "imaging/EffectsSettings.h"

#include <QImage>

namespace imaging {

class EffectsProcessor final
{
public:
    EffectsProcessor() = delete;

    // Applies effects to an image that is already in its final post-crop geometry.
    // pixelScale is the rendered-resolution scale relative to the full-resolution
    // image, allowing deterministic grain to match between preview and export.
    [[nodiscard]] static QImage apply(const QImage &image,
                                      const EffectsSettings &settings,
                                      double pixelScale = 1.0);

    // Applies the same post-crop effect coordinates to a develop image before
    // Apollo's non-destructive canvas geometry transform is displayed.
    [[nodiscard]] static QImage applyForCanvas(const QImage &image,
                                               const DevelopState &state,
                                               double pixelScale = 1.0);
};

} // namespace imaging
