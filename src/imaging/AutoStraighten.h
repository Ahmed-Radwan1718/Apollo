#pragma once

#include <QImage>
#include <QtGlobal>

namespace imaging {

struct AutoStraightenResult
{
    bool valid = false;
    qreal degrees = 0.0;
    qreal confidence = 0.0;
};

[[nodiscard]] AutoStraightenResult estimateAutoStraighten(const QImage &image,
                                                           int quarterTurns);

} // namespace imaging
