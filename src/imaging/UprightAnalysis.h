#pragma once

#include "app/DevelopState.h"

#include <QImage>
#include <QPointF>
#include <QSizeF>
#include <QtGlobal>

#include <vector>

namespace imaging {

enum class UprightMode : int {
    Off = 0,
    Auto = 1,
    Guided = 2,
    Level = 3,
    Vertical = 4,
    Full = 5,
};

enum class UprightGuideAxis : int {
    Horizontal = 0,
    Vertical = 1,
};

struct UprightGuideConstraint
{
    QPointF startNormalized;
    QPointF endNormalized;
    UprightGuideAxis axis = UprightGuideAxis::Horizontal;
};

struct UprightCorrection
{
    bool valid = false;
    int vertical = 0;
    int horizontal = 0;
    int rotateHundredths = 0;
    qreal score = 0.0;
};

[[nodiscard]] UprightCorrection estimateAutomaticUpright(const QImage &image,
                                                          const DevelopState &state,
                                                          UprightMode mode);

[[nodiscard]] UprightCorrection estimateGuidedUpright(
    const QSizeF &sourceSize,
    const DevelopState &state,
    const std::vector<UprightGuideConstraint> &guides);

} // namespace imaging
