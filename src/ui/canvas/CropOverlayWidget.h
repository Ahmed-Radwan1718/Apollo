#pragma once

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QLineF>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QRectF>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <utility>

inline constexpr qreal kCropOverlayMinimumPixels = 28.0;
inline constexpr qreal kCropOverlayHandleHitRadius = 9.0;

class CropOverlayWidget final : public QWidget
{
public:
    using ChangeCallback = std::function<void(const QRectF &, bool)>;

    explicit CropOverlayWidget(QGraphicsView *view, QWidget *parent = nullptr)
        : QWidget(parent)
        , m_view(view)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        hide();
    }

    void setChangeCallback(ChangeCallback callback)
    {
        m_changeCallback = std::move(callback);
    }

    void setNormalizedRect(const QRectF &rect)
    {
        if (m_dragging) {
            return;
        }
        m_normalizedRect = constrainedToAllowed(rect, 0.001, 0.001);
        update();
    }

    void setAspectRatio(qreal ratio)
    {
        m_pixelAspectRatio = std::max<qreal>(0.0, ratio);
    }

    void setAllowedRect(const QRectF &rect)
    {
        m_allowedRect = rect.isValid() && !rect.isEmpty()
            ? rect.intersected(QRectF(0.0, 0.0, 1.0, 1.0))
            : QRectF(0.0, 0.0, 1.0, 1.0);
        m_normalizedRect = constrainedToAllowed(m_normalizedRect, 0.001, 0.001);
    }

    void setEditing(bool editing)
    {
        m_editing = editing;
        setAttribute(Qt::WA_TransparentForMouseEvents, !editing);
        updateCursor(QPointF(-100.0, -100.0));
        update();
    }

    [[nodiscard]] QRectF imageViewportRect() const
    {
        QGraphicsItem *item = imageItem();
        if (!item || !m_view) {
            return {};
        }

        return QRectF(m_view->mapFromScene(item->sceneBoundingRect()).boundingRect());
    }

    [[nodiscard]] QRectF cropViewportRect() const
    {
        QGraphicsItem *item = imageItem();
        if (!item || !m_view) {
            return {};
        }

        const QRectF bounds = item->sceneBoundingRect();
        if (bounds.isEmpty()) {
            return {};
        }

        const QRectF cropScene(
            bounds.left() + m_normalizedRect.left() * bounds.width(),
            bounds.top() + m_normalizedRect.top() * bounds.height(),
            m_normalizedRect.width() * bounds.width(),
            m_normalizedRect.height() * bounds.height());
        return QRectF(m_view->mapFromScene(cropScene).boundingRect());
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        const QRectF cropRect = cropViewportRect();
        if (cropRect.isEmpty()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        QPainterPath outside;
        outside.setFillRule(Qt::OddEvenFill);
        outside.addRect(QRectF(rect()));
        outside.addRect(cropRect);
        painter.fillPath(outside,
                         m_editing ? QColor(0, 0, 0, 0)
                                   : QColor(24, 24, 24));

        if (!m_editing) {
            return;
        }

        painter.setPen(QPen(QColor(238, 238, 238), 1.15));
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(cropRect);

        painter.setPen(QPen(QColor(255, 255, 255, 120), 0.8));
        for (int i = 1; i <= 2; ++i) {
            const qreal x = cropRect.left() + cropRect.width() * static_cast<qreal>(i) / 3.0;
            const qreal y = cropRect.top() + cropRect.height() * static_cast<qreal>(i) / 3.0;
            painter.drawLine(QPointF(x, cropRect.top()), QPointF(x, cropRect.bottom()));
            painter.drawLine(QPointF(cropRect.left(), y), QPointF(cropRect.right(), y));
        }

        painter.setPen(QPen(QColor(30, 30, 30), 1.0));
        painter.setBrush(QColor(242, 242, 242));
        for (const QPointF &point : handlePoints(cropRect)) {
            painter.drawRoundedRect(QRectF(point.x() - 3.5,
                                           point.y() - 3.5,
                                           7.0,
                                           7.0),
                                    1.4,
                                    1.4);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_editing || event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        const QRectF cropRect = cropViewportRect();
        m_handle = hitTest(event->position(), cropRect);
        if (m_handle == Handle::None) {
            event->ignore();
            return;
        }

        m_dragging = true;
        m_dragStartPosition = event->position();
        m_dragStartRect = m_normalizedRect;
        event->accept();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_editing) {
            event->ignore();
            return;
        }

        if (!m_dragging) {
            updateCursor(event->position());
            event->accept();
            return;
        }

        const QRectF imageRect = imageViewportRect();
        if (imageRect.width() <= 1.0 || imageRect.height() <= 1.0) {
            event->accept();
            return;
        }

        const QPointF delta = event->position() - m_dragStartPosition;
        const QPointF normalizedDelta(delta.x() / imageRect.width(),
                                      delta.y() / imageRect.height());
        const qreal minWidth = std::min<qreal>(1.0, kCropOverlayMinimumPixels / imageRect.width());
        const qreal minHeight = std::min<qreal>(1.0, kCropOverlayMinimumPixels / imageRect.height());

        QRectF next = m_pixelAspectRatio > 0.0
            ? resizedFixedAspect(normalizedDelta, imageRect, minWidth, minHeight)
            : resizedFree(normalizedDelta, minWidth, minHeight);
        next = constrainedToAllowed(next, minWidth, minHeight);

        if (next != m_normalizedRect) {
            m_normalizedRect = next;
            if (m_changeCallback) {
                m_changeCallback(m_normalizedRect, false);
            }
            update();
        }

        event->accept();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_dragging || event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        m_dragging = false;
        if (m_changeCallback) {
            m_changeCallback(m_normalizedRect, true);
        }
        updateCursor(event->position());
        event->accept();
    }

    void leaveEvent(QEvent *event) override
    {
        QWidget::leaveEvent(event);
        if (!m_dragging) {
            unsetCursor();
        }
    }

private:
    enum class Handle {
        None,
        Move,
        Left,
        Right,
        Top,
        Bottom,
        TopLeft,
        TopRight,
        BottomLeft,
        BottomRight,
    };

    QGraphicsItem *imageItem() const
    {
        if (!m_view || !m_view->scene()) {
            return nullptr;
        }

        const QList<QGraphicsItem *> items = m_view->scene()->items(Qt::AscendingOrder);
        for (QGraphicsItem *item : items) {
            if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
                return item;
            }
        }
        return nullptr;
    }

    static QList<QPointF> handlePoints(const QRectF &rect)
    {
        return {
            rect.topLeft(),
            QPointF(rect.center().x(), rect.top()),
            rect.topRight(),
            QPointF(rect.left(), rect.center().y()),
            QPointF(rect.right(), rect.center().y()),
            rect.bottomLeft(),
            QPointF(rect.center().x(), rect.bottom()),
            rect.bottomRight(),
        };
    }

    Handle hitTest(const QPointF &position, const QRectF &cropRect) const
    {
        const auto nearPoint = [&](const QPointF &point) {
            return QLineF(position, point).length() <= kCropOverlayHandleHitRadius;
        };

        if (nearPoint(cropRect.topLeft())) return Handle::TopLeft;
        if (nearPoint(cropRect.topRight())) return Handle::TopRight;
        if (nearPoint(cropRect.bottomLeft())) return Handle::BottomLeft;
        if (nearPoint(cropRect.bottomRight())) return Handle::BottomRight;
        if (nearPoint(QPointF(cropRect.center().x(), cropRect.top()))) return Handle::Top;
        if (nearPoint(QPointF(cropRect.center().x(), cropRect.bottom()))) return Handle::Bottom;
        if (nearPoint(QPointF(cropRect.left(), cropRect.center().y()))) return Handle::Left;
        if (nearPoint(QPointF(cropRect.right(), cropRect.center().y()))) return Handle::Right;
        if (cropRect.contains(position)) return Handle::Move;
        return Handle::None;
    }

    void updateCursor(const QPointF &position)
    {
        if (!m_editing) {
            unsetCursor();
            return;
        }

        const Handle handle = hitTest(position, cropViewportRect());
        switch (handle) {
        case Handle::Move:
            setCursor(Qt::SizeAllCursor);
            break;
        case Handle::Left:
        case Handle::Right:
            setCursor(Qt::SizeHorCursor);
            break;
        case Handle::Top:
        case Handle::Bottom:
            setCursor(Qt::SizeVerCursor);
            break;
        case Handle::TopLeft:
        case Handle::BottomRight:
            setCursor(Qt::SizeFDiagCursor);
            break;
        case Handle::TopRight:
        case Handle::BottomLeft:
            setCursor(Qt::SizeBDiagCursor);
            break;
        case Handle::None:
        default:
            unsetCursor();
            break;
        }
    }

    QRectF resizedFree(const QPointF &delta, qreal minWidth, qreal minHeight) const
    {
        QRectF rect = m_dragStartRect;
        if (m_handle == Handle::Move) {
            rect.translate(delta);
            rect.moveLeft(std::clamp(rect.left(), 0.0, 1.0 - rect.width()));
            rect.moveTop(std::clamp(rect.top(), 0.0, 1.0 - rect.height()));
            return rect;
        }

        qreal left = rect.left();
        qreal right = rect.right();
        qreal top = rect.top();
        qreal bottom = rect.bottom();

        const bool moveLeft = m_handle == Handle::Left
            || m_handle == Handle::TopLeft || m_handle == Handle::BottomLeft;
        const bool moveRight = m_handle == Handle::Right
            || m_handle == Handle::TopRight || m_handle == Handle::BottomRight;
        const bool moveTop = m_handle == Handle::Top
            || m_handle == Handle::TopLeft || m_handle == Handle::TopRight;
        const bool moveBottom = m_handle == Handle::Bottom
            || m_handle == Handle::BottomLeft || m_handle == Handle::BottomRight;

        if (moveLeft) left = std::clamp(left + delta.x(), 0.0, right - minWidth);
        if (moveRight) right = std::clamp(right + delta.x(), left + minWidth, 1.0);
        if (moveTop) top = std::clamp(top + delta.y(), 0.0, bottom - minHeight);
        if (moveBottom) bottom = std::clamp(bottom + delta.y(), top + minHeight, 1.0);

        return QRectF(QPointF(left, top), QPointF(right, bottom));
    }

    QRectF resizedFixedAspect(const QPointF &delta,
                              const QRectF &imageRect,
                              qreal minWidth,
                              qreal minHeight) const
    {
        if (m_handle == Handle::Move) {
            return resizedFree(delta, minWidth, minHeight);
        }

        const qreal imageAspect = imageRect.width() / imageRect.height();
        const qreal normalizedRatio = std::max<qreal>(0.001,
            m_pixelAspectRatio / std::max<qreal>(0.001, imageAspect));
        const QRectF start = m_dragStartRect;

        const bool left = m_handle == Handle::Left
            || m_handle == Handle::TopLeft || m_handle == Handle::BottomLeft;
        const bool right = m_handle == Handle::Right
            || m_handle == Handle::TopRight || m_handle == Handle::BottomRight;
        const bool top = m_handle == Handle::Top
            || m_handle == Handle::TopLeft || m_handle == Handle::TopRight;
        const bool bottom = m_handle == Handle::Bottom
            || m_handle == Handle::BottomLeft || m_handle == Handle::BottomRight;

        const bool corner = (left || right) && (top || bottom);
        if (corner) {
            const QPointF anchor(left ? start.right() : start.left(),
                                 top ? start.bottom() : start.top());
            const qreal pointerX = std::clamp(
                (left ? start.left() : start.right()) + delta.x(), 0.0, 1.0);
            const qreal pointerY = std::clamp(
                (top ? start.top() : start.bottom()) + delta.y(), 0.0, 1.0);
            const qreal maxWidth = left ? anchor.x() : 1.0 - anchor.x();
            const qreal maxHeight = top ? anchor.y() : 1.0 - anchor.y();
            qreal width = std::abs(pointerX - anchor.x());
            qreal height = std::abs(pointerY - anchor.y());

            if (height <= 0.0001 || width / height >= normalizedRatio) {
                height = width / normalizedRatio;
            } else {
                width = height * normalizedRatio;
            }

            if (width > maxWidth) {
                width = maxWidth;
                height = width / normalizedRatio;
            }
            if (height > maxHeight) {
                height = maxHeight;
                width = height * normalizedRatio;
            }

            width = std::max(width, minWidth);
            height = std::max(height, minHeight);
            if (width / height > normalizedRatio) {
                width = height * normalizedRatio;
            } else {
                height = width / normalizedRatio;
            }

            const qreal x = left ? anchor.x() - width : anchor.x();
            const qreal y = top ? anchor.y() - height : anchor.y();
            return QRectF(x, y, width, height);
        }

        if (left || right) {
            const qreal anchorX = left ? start.right() : start.left();
            qreal edgeX = (left ? start.left() : start.right()) + delta.x();
            edgeX = std::clamp(edgeX, 0.0, 1.0);
            qreal width = std::max(minWidth, std::abs(edgeX - anchorX));
            qreal height = std::max(minHeight, width / normalizedRatio);
            const qreal centerY = start.center().y();
            const qreal maxHeight = 2.0 * std::min(centerY, 1.0 - centerY);
            if (height > maxHeight) {
                height = maxHeight;
                width = height * normalizedRatio;
            }
            width = std::min(width, left ? anchorX : 1.0 - anchorX);
            height = width / normalizedRatio;
            return QRectF(left ? anchorX - width : anchorX,
                          centerY - height * 0.5,
                          width,
                          height);
        }

        const qreal anchorY = top ? start.bottom() : start.top();
        qreal edgeY = (top ? start.top() : start.bottom()) + delta.y();
        edgeY = std::clamp(edgeY, 0.0, 1.0);
        qreal height = std::max(minHeight, std::abs(edgeY - anchorY));
        qreal width = std::max(minWidth, height * normalizedRatio);
        const qreal centerX = start.center().x();
        const qreal maxWidth = 2.0 * std::min(centerX, 1.0 - centerX);
        if (width > maxWidth) {
            width = maxWidth;
            height = width / normalizedRatio;
        }
        height = std::min(height, top ? anchorY : 1.0 - anchorY);
        width = height * normalizedRatio;
        return QRectF(centerX - width * 0.5,
                      top ? anchorY - height : anchorY,
                      width,
                      height);
    }


    QRectF constrainedToAllowed(QRectF rect, qreal minWidth, qreal minHeight) const
    {
        const QRectF bounds = m_allowedRect.isValid() && !m_allowedRect.isEmpty()
            ? m_allowedRect
            : QRectF(0.0, 0.0, 1.0, 1.0);
        rect = rect.normalized();

        const qreal effectiveMinWidth = std::min(std::max<qreal>(0.001, minWidth), bounds.width());
        const qreal effectiveMinHeight = std::min(std::max<qreal>(0.001, minHeight), bounds.height());
        const qreal width = std::clamp(rect.width(), effectiveMinWidth, bounds.width());
        const qreal height = std::clamp(rect.height(), effectiveMinHeight, bounds.height());
        const qreal x = std::clamp(rect.x(), bounds.left(), bounds.right() - width);
        const qreal y = std::clamp(rect.y(), bounds.top(), bounds.bottom() - height);
        return QRectF(x, y, width, height);
    }

    QGraphicsView *m_view = nullptr;
    QRectF m_normalizedRect{0.0, 0.0, 1.0, 1.0};
    QRectF m_allowedRect{0.0, 0.0, 1.0, 1.0};
    qreal m_pixelAspectRatio = 0.0;
    ChangeCallback m_changeCallback;

    bool m_editing = false;
    bool m_dragging = false;
    Handle m_handle = Handle::None;
    QPointF m_dragStartPosition;
    QRectF m_dragStartRect;
};
