#pragma once

#include "imaging/AutoHighlights.h"
#include "ui/canvas/PhotoCanvas.h"

#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QObject>
#include <QPainter>
#include <QPointer>
#include <QSize>
#include <QSlider>
#include <QStyleOptionGraphicsItem>
#include <QWidget>

#include <algorithm>
#include <cmath>

class AutoHighlightsController final : public QObject
{
public:
    explicit AutoHighlightsController(QWidget *basicPanel,
                                      QObject *parent = nullptr)
        : QObject(parent)
        , m_basicPanel(basicPanel)
    {
        if (!m_basicPanel) {
            return;
        }

        QWidget *root = m_basicPanel->window();
        m_canvas = root ? root->findChild<PhotoCanvas *>() : nullptr;
        if (!m_canvas) {
            return;
        }

        for (QLabel *label : m_basicPanel->findChildren<QLabel *>()) {
            if (label && label->text() == QStringLiteral("Highlights")) {
                m_highlightsLabel = label;
                break;
            }
        }

        if (m_highlightsLabel) {
            QWidget *control = m_highlightsLabel->parentWidget();
            m_highlightsSlider = control
                ? control->findChild<QSlider *>(QString(), Qt::FindDirectChildrenOnly)
                : nullptr;
        }

        if (m_highlightsLabel && m_highlightsSlider
            && m_highlightsSlider->minimum() == -100
            && m_highlightsSlider->maximum() == 100) {
            m_highlightsLabel->installEventFilter(this);
        } else {
            m_highlightsSlider = nullptr;
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_highlightsLabel.data()
            && event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && mouseEvent->modifiers().testFlag(Qt::ShiftModifier)) {
                applyAutoHighlights();
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void applyAutoHighlights()
    {
        if (!m_canvas || !m_canvas->hasImage() || !m_highlightsSlider) {
            return;
        }

        QGraphicsItem *item = imageItem();
        if (!item) {
            return;
        }

        const QImage image = analysisImage(item);
        if (image.isNull()) {
            return;
        }

        const imaging::AutoHighlightsResult estimate =
            imaging::estimateAutoHighlights(image);
        if (!estimate.valid) {
            return;
        }

        const int correction = static_cast<int>(std::lround(estimate.deltaValue));
        const int targetValue = std::clamp(
            m_highlightsSlider->value() + correction,
            m_highlightsSlider->minimum(),
            m_highlightsSlider->maximum());

        if (targetValue == m_highlightsSlider->value()) {
            return;
        }

        const bool wasSliderDown = m_highlightsSlider->isSliderDown();
        if (!wasSliderDown) {
            m_highlightsSlider->setSliderDown(true);
        }
        m_highlightsSlider->setValue(targetValue);
        if (!wasSliderDown) {
            m_highlightsSlider->setSliderDown(false);
        }
    }

    [[nodiscard]] QGraphicsItem *imageItem() const
    {
        if (!m_canvas || !m_canvas->scene()) {
            return nullptr;
        }

        const auto items = m_canvas->scene()->items(Qt::AscendingOrder);
        for (QGraphicsItem *item : items) {
            if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
                return item;
            }
        }
        return nullptr;
    }

    [[nodiscard]] QImage analysisImage(QGraphicsItem *item) const
    {
        if (!item) {
            return {};
        }

        const QRectF bounds = item->boundingRect();
        if (!bounds.isValid() || bounds.isEmpty()) {
            return {};
        }

        QSize targetSize = bounds.size().toSize();
        if (!targetSize.isValid() || targetSize.isEmpty()) {
            return {};
        }
        targetSize.scale(QSize(1200, 1200), Qt::KeepAspectRatio);
        targetSize.setWidth(std::max(1, targetSize.width()));
        targetSize.setHeight(std::max(1, targetSize.height()));

        QImage image(targetSize, QImage::Format_RGB32);
        image.fill(Qt::black);

        QPainter painter(&image);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.scale(static_cast<qreal>(targetSize.width()) / bounds.width(),
                      static_cast<qreal>(targetSize.height()) / bounds.height());
        painter.translate(-bounds.left(), -bounds.top());
        QStyleOptionGraphicsItem option;
        item->paint(&painter, &option, nullptr);
        painter.end();

        return image;
    }

    QPointer<QWidget> m_basicPanel;
    QPointer<PhotoCanvas> m_canvas;
    QPointer<QLabel> m_highlightsLabel;
    QPointer<QSlider> m_highlightsSlider;
};
