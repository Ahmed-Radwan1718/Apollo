#pragma once

#include <QtGlobal>

#include <cmath>

namespace imaging {
namespace detail {

inline qreal srgbToLinear(int channel)
{
    const qreal value = static_cast<qreal>(channel) / 255.0;
    if (value <= 0.04045) {
        return value / 12.92;
    }
    return std::pow((value + 0.055) / 1.055, 2.4);
}

} // namespace detail
} // namespace imaging
