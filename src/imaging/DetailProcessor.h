#pragma once

#include "imaging/DetailSettings.h"

#include <QImage>

namespace imaging {

class DetailProcessor final
{
public:
    DetailProcessor() = delete;

    [[nodiscard]] static QImage apply(const QImage &image,
                                      const DetailSettings &settings,
                                      double radiusScale = 1.0);
};

} // namespace imaging
