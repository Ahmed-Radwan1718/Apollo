#pragma once

#include "imaging/UprightAnalysis.h"

#include <QColor>
#include <QEvent>
#include <QLineF>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
#include <QWidget>

#include <algorithm>
#include <functional>
#include <optional>
#include <vector>

class UprightGuideOverlay final : public QWidget
{
public:
    using ToSourceCallback =
        std::function<std::optional<QPointF>(const QPointF &viewportPoint)>;
    using ToViewportCallback =
        std::function<std::optional<QPointF>(const QPointF &sourceNormalizedPoint)>;
    using GuidesChangedCallback =
        std::function<void(const std::vector<imaging::UprightGuideConstraint> &)>;

    explicit UprightGuideOverlay(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
        setAttribute(Qt::WA_TranslucentBackground);
        setFocusPolicy(Qt::NoFocus);
        setMouseTracking(true);
        setActive(false);
        if (parent) {
            parent->installEventFilter(this);
            setGeometry(parent->rect());
        }
    }

    void setMappingCallbacks(ToSourceCallback toSource,
                             ToViewportCallback toViewport)
    {
        m_toSource = std::move(toSource);
        m_toViewport = std::move(toViewport);
        update();
    }

    void setGuidesChangedCallback(GuidesChangedCallback callback)
    {
        m_guidesChanged = std::move(callback);
    }

    void setActive(bool active)
    {
        m_active = active;
        setAttribute(Qt::WA_TransparentForMouseEvents, !active);
        setCursor(active ? Qt::CrossCursor : Qt::ArrowCursor);
        setVisible(active);
        if (active) {
            raise();
            update();
        } else {
            m_dragging = false;
        }
    }

    [[nodiscard]] bool isActive() const noexcept
    {
        return m_active;
    }

    void clearGuides()
    {
        m_guides.clear();
        m_dragging = false;
        update();
    }

    [[nodiscard]] const std::vector<imaging::UprightGuideConstraint> &guides() const noexcept
    {
        return m_guides;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parentWidget()
            && (event->type() == QEvent::Resize
                || event->type() == QEvent::Show)) {
            setGeometry(parentWidget()->rect());
            if (m_active) {
                raise();
                update();
            }
        }
        return QWidget::eventFilter(watched, event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        if (!m_active) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        for (const imaging::UprightGuideConstraint &guide : m_guides) {
            if (!m_toViewport) {
                break;
            }
            const std::optional<QPointF> start = m_toViewport(guide.startNormalized);
            const std::optional<QPointF> end = m_toViewport(guide.endNormalized);
            if (!start || !end) {
                continue;
            }
            drawGuide(painter, *start, *end);
        }

        if (m_dragging) {
            drawGuide(painter, m_dragStart, m_dragCurrent);
        }
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (!m_active || event->button() != Qt::LeftButton || m_guides.size() >= 4) {
            event->ignore();
            return;
        }

        m_dragging = true;
        m_dragStart = event->position();
        m_dragCurrent = m_dragStart;
        event->accept();
        update();
    }

    void mouseMoveEvent(QMouseEvent *event) override
    {
        if (!m_dragging) {
            event->ignore();
            return;
        }
        m_dragCurrent = event->position();
        event->accept();
        update();
    }

    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (!m_dragging || event->button() != Qt::LeftButton) {
            event->ignore();
            return;
        }

        m_dragging = false;
        m_dragCurrent = event->position();
        const QLineF line(m_dragStart, m_dragCurrent);
        if (line.length() >= 12.0 && m_toSource && m_guides.size() < 4) {
            const std::optional<QPointF> start = m_toSource(m_dragStart);
            const std::optional<QPointF> end = m_toSource(m_dragCurrent);
            if (start && end) {
                imaging::UprightGuideConstraint guide;
                guide.startNormalized = *start;
                guide.endNormalized = *end;
                guide.axis = std::abs(line.dx()) >= std::abs(line.dy())
                    ? imaging::UprightGuideAxis::Horizontal
                    : imaging::UprightGuideAxis::Vertical;
                m_guides.push_back(guide);
                if (m_guidesChanged) {
                    m_guidesChanged(m_guides);
                }
            }
        }

        event->accept();
        update();
    }

private:
    static void drawGuide(QPainter &painter,
                          const QPointF &start,
                          const QPointF &end)
    {
        painter.setPen(QPen(QColor(0, 0, 0, 175),
                            3.0,
                            Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(start, end);
        painter.setPen(QPen(QColor(235, 235, 235, 235),
                            1.25,
                            Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(start, end);
        painter.setBrush(QColor(235, 235, 235, 235));
        painter.setPen(QPen(QColor(0, 0, 0, 160), 1.0));
        painter.drawEllipse(start, 2.5, 2.5);
        painter.drawEllipse(end, 2.5, 2.5);
    }

    ToSourceCallback m_toSource;
    ToViewportCallback m_toViewport;
    GuidesChangedCallback m_guidesChanged;
    std::vector<imaging::UprightGuideConstraint> m_guides;
    QPointF m_dragStart;
    QPointF m_dragCurrent;
    bool m_active = false;
    bool m_dragging = false;
};
