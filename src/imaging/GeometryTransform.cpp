#include "imaging/GeometryTransform.h"

#include "imaging/CropTransform.h"

#include <QLineF>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace imaging {
namespace {

constexpr qreal kMaximumPerspectiveInset = 0.23;
constexpr int kSafeRectMaxGrid = 768;
constexpr int kSafeRectMinGrid = 192;
constexpr qreal kBoundaryEpsilon = 1.0e-7;

QPolygonF sourceQuad(const QSizeF &size)
{
    return {
        QPointF(0.0, 0.0),
        QPointF(size.width(), 0.0),
        QPointF(size.width(), size.height()),
        QPointF(0.0, size.height()),
    };
}

QPolygonF canvasQuad(const QRectF &rect)
{
    return {
        rect.topLeft(),
        rect.topRight(),
        rect.bottomRight(),
        rect.bottomLeft(),
    };
}

bool pointOnPolygonBoundary(const QPointF &point, const QPolygonF &polygon)
{
    if (polygon.size() < 2) {
        return false;
    }

    for (qsizetype index = 0; index < polygon.size(); ++index) {
        const QPointF a = polygon.at(index);
        const QPointF b = polygon.at((index + 1) % polygon.size());
        const QLineF edge(a, b);
        if (edge.length() <= kBoundaryEpsilon) {
            continue;
        }

        const qreal cross = (point.x() - a.x()) * (b.y() - a.y())
            - (point.y() - a.y()) * (b.x() - a.x());
        if (std::abs(cross) > kBoundaryEpsilon * std::max<qreal>(1.0, edge.length())) {
            continue;
        }

        const qreal dot = (point.x() - a.x()) * (b.x() - a.x())
            + (point.y() - a.y()) * (b.y() - a.y());
        const qreal lengthSquared = edge.length() * edge.length();
        if (dot >= -kBoundaryEpsilon && dot <= lengthSquared + kBoundaryEpsilon) {
            return true;
        }
    }
    return false;
}

bool polygonContainsPoint(const QPolygonF &polygon, const QPointF &point)
{
    return polygon.containsPoint(point, Qt::OddEvenFill)
        || pointOnPolygonBoundary(point, polygon);
}

bool rectInsidePolygon(const QRectF &rect, const QPolygonF &polygon)
{
    if (!rect.isValid() || rect.isEmpty()) {
        return false;
    }

    return polygonContainsPoint(polygon, rect.topLeft())
        && polygonContainsPoint(polygon, rect.topRight())
        && polygonContainsPoint(polygon, rect.bottomRight())
        && polygonContainsPoint(polygon, rect.bottomLeft());
}

QRectF refinedSafeRect(QRectF rect,
                       const QRectF &canvas,
                       const QPolygonF &polygon)
{
    if (rect.isEmpty()) {
        return rect;
    }

    const auto expandEdge = [&](int edge) {
        qreal inside = 0.0;
        qreal outside = 0.0;
        switch (edge) {
        case 0:
            inside = rect.left();
            outside = canvas.left();
            break;
        case 1:
            inside = rect.right();
            outside = canvas.right();
            break;
        case 2:
            inside = rect.top();
            outside = canvas.top();
            break;
        default:
            inside = rect.bottom();
            outside = canvas.bottom();
            break;
        }

        for (int iteration = 0; iteration < 24; ++iteration) {
            const qreal candidate = 0.5 * (inside + outside);
            QRectF test = rect;
            if (edge == 0) test.setLeft(candidate);
            if (edge == 1) test.setRight(candidate);
            if (edge == 2) test.setTop(candidate);
            if (edge == 3) test.setBottom(candidate);

            if (rectInsidePolygon(test, polygon)) {
                inside = candidate;
                rect = test;
            } else {
                outside = candidate;
            }
        }
    };

    for (int pass = 0; pass < 2; ++pass) {
        expandEdge(0);
        expandEdge(1);
        expandEdge(2);
        expandEdge(3);
    }
    return rect;
}

QRectF largestSafeRect(const QRectF &canvas, const QPolygonF &contentPolygon)
{
    if (!canvas.isValid() || canvas.isEmpty() || contentPolygon.size() != 4) {
        return {};
    }

    if (rectInsidePolygon(canvas, contentPolygon)) {
        return canvas;
    }

    const qreal aspect = canvas.width() / canvas.height();
    int gridWidth = kSafeRectMaxGrid;
    int gridHeight = kSafeRectMaxGrid;
    if (aspect >= 1.0) {
        gridHeight = std::max(kSafeRectMinGrid,
                              static_cast<int>(std::lround(kSafeRectMaxGrid / aspect)));
    } else {
        gridWidth = std::max(kSafeRectMinGrid,
                             static_cast<int>(std::lround(kSafeRectMaxGrid * aspect)));
    }

    const qreal cellWidth = canvas.width() / static_cast<qreal>(gridWidth);
    const qreal cellHeight = canvas.height() / static_cast<qreal>(gridHeight);
    std::vector<int> heights(static_cast<std::size_t>(gridWidth), 0);

    qint64 bestArea = 0;
    int bestLeft = 0;
    int bestRight = 0;
    int bestTop = 0;
    int bestBottom = 0;

    for (int row = 0; row < gridHeight; ++row) {
        const qreal top = canvas.top() + row * cellHeight;
        const qreal bottom = canvas.top() + (row + 1) * cellHeight;

        for (int column = 0; column < gridWidth; ++column) {
            const qreal left = canvas.left() + column * cellWidth;
            const qreal right = canvas.left() + (column + 1) * cellWidth;
            const QRectF cell(QPointF(left, top), QPointF(right, bottom));
            heights[static_cast<std::size_t>(column)] =
                rectInsidePolygon(cell, contentPolygon)
                ? heights[static_cast<std::size_t>(column)] + 1
                : 0;
        }

        std::vector<int> stack;
        stack.reserve(static_cast<std::size_t>(gridWidth + 1));
        for (int column = 0; column <= gridWidth; ++column) {
            const int currentHeight = column < gridWidth
                ? heights[static_cast<std::size_t>(column)]
                : 0;

            while (!stack.empty()
                   && heights[static_cast<std::size_t>(stack.back())] > currentHeight) {
                const int height = heights[static_cast<std::size_t>(stack.back())];
                stack.pop_back();
                const int leftColumn = stack.empty() ? 0 : stack.back() + 1;
                const int rightColumn = column;
                const qint64 area = static_cast<qint64>(height)
                    * static_cast<qint64>(rightColumn - leftColumn);
                if (area > bestArea) {
                    bestArea = area;
                    bestLeft = leftColumn;
                    bestRight = rightColumn;
                    bestBottom = row + 1;
                    bestTop = bestBottom - height;
                }
            }
            stack.push_back(column);
        }
    }

    if (bestArea <= 0 || bestRight <= bestLeft || bestBottom <= bestTop) {
        return {};
    }

    QRectF safe(
        canvas.left() + bestLeft * cellWidth,
        canvas.top() + bestTop * cellHeight,
        (bestRight - bestLeft) * cellWidth,
        (bestBottom - bestTop) * cellHeight);
    return refinedSafeRect(safe, canvas, contentPolygon).intersected(canvas);
}

QPointF applyAffineControls(const QPointF &point,
                            const QRectF &canvas,
                            const DevelopState &state)
{
    const QPointF center = canvas.center();
    const qreal scale = std::clamp(state.transformScale, 50, 150) / 100.0;
    const qreal aspect = std::clamp(state.transformAspect, -100, 100);
    const qreal aspectScale = std::exp2(aspect / 200.0);
    const qreal radians = std::clamp(state.transformRotateHundredths, -1000, 1000)
        / 100.0 * std::numbers::pi_v<qreal> / 180.0;
    const qreal cosine = std::cos(radians);
    const qreal sine = std::sin(radians);

    qreal x = (point.x() - center.x()) * scale * aspectScale;
    qreal y = (point.y() - center.y()) * scale;
    const qreal rotatedX = x * cosine - y * sine;
    const qreal rotatedY = x * sine + y * cosine;

    const qreal offsetX = std::clamp(state.transformXOffset, -100, 100)
        / 100.0 * canvas.width() * 0.5;
    const qreal offsetY = std::clamp(state.transformYOffset, -100, 100)
        / 100.0 * canvas.height() * 0.5;

    return QPointF(center.x() + rotatedX + offsetX,
                   center.y() + rotatedY + offsetY);
}

} // namespace

bool hasManualTransform(const DevelopState &state) noexcept
{
    return state.transformVertical != 0
        || state.transformHorizontal != 0
        || state.transformRotateHundredths != 0
        || state.transformAspect != 0
        || state.transformScale != 100
        || state.transformXOffset != 0
        || state.transformYOffset != 0;
}

GeometryMapping geometryMappingForState(const QSizeF &sourceSize,
                                        const DevelopState &state,
                                        bool computeSafeCrop)
{
    GeometryMapping result;
    if (!sourceSize.isValid() || sourceSize.isEmpty()) {
        return result;
    }

    const QPolygonF source = sourceQuad(sourceSize);

    QTransform rotate;
    rotate.rotate(cropRotationDegrees(state));
    QPolygonF oriented = rotate.map(source);
    const QRectF orientedBounds = oriented.boundingRect();
    if (!orientedBounds.isValid() || orientedBounds.isEmpty()) {
        return result;
    }

    for (QPointF &point : oriented) {
        point -= orientedBounds.topLeft();
    }

    result.canvasRect = QRectF(QPointF(0.0, 0.0), orientedBounds.size());
    const QPolygonF canvas = canvasQuad(result.canvasRect);
    QPolygonF perspectiveTarget = canvas;

    const qreal vertical = std::clamp(state.transformVertical, -100, 100) / 100.0;
    const qreal horizontal = std::clamp(state.transformHorizontal, -100, 100) / 100.0;
    const qreal insetX = std::abs(vertical) * kMaximumPerspectiveInset
        * result.canvasRect.width();
    const qreal insetY = std::abs(horizontal) * kMaximumPerspectiveInset
        * result.canvasRect.height();

    if (vertical > 0.0) {
        perspectiveTarget[0].rx() += insetX;
        perspectiveTarget[1].rx() -= insetX;
    } else if (vertical < 0.0) {
        perspectiveTarget[3].rx() += insetX;
        perspectiveTarget[2].rx() -= insetX;
    }

    if (horizontal > 0.0) {
        perspectiveTarget[0].ry() += insetY;
        perspectiveTarget[3].ry() -= insetY;
    } else if (horizontal < 0.0) {
        perspectiveTarget[1].ry() += insetY;
        perspectiveTarget[2].ry() -= insetY;
    }

    QTransform perspective;
    if (!QTransform::quadToQuad(canvas, perspectiveTarget, perspective)
        || !perspective.isInvertible()) {
        return result;
    }

    QPolygonF destination = perspective.map(oriented);
    for (QPointF &point : destination) {
        point = applyAffineControls(point, result.canvasRect, state);
    }

    QTransform complete;
    if (!QTransform::quadToQuad(source, destination, complete) || !complete.isInvertible()) {
        return result;
    }

    result.valid = true;
    result.transform = complete;
    result.contentPolygon = destination;

    if (computeSafeCrop) {
        const QRectF safe = largestSafeRect(result.canvasRect, result.contentPolygon);
        if (safe.isValid() && !safe.isEmpty()) {
            result.safeNormalizedRect = QRectF(
                (safe.left() - result.canvasRect.left()) / result.canvasRect.width(),
                (safe.top() - result.canvasRect.top()) / result.canvasRect.height(),
                safe.width() / result.canvasRect.width(),
                safe.height() / result.canvasRect.height());
        }
    }

    return result;
}

} // namespace imaging
