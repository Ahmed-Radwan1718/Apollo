#pragma once

#include "imaging/AutoExposure.h"
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

class AutoExposureController final : public QObject
{
public:
    explicit AutoExposureController(QWidget *basicPanel,
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
            if (label && label->text() == QStringLiteral("Exposure")) {
                m_exposureLabel = label;
                break;
            }
        }

        for (QSlider *slider : m_basicPanel->findChildren<QSlider *>()) {
            if (slider
                && slider->orientation() == Qt::Horizontal
                && slider->minimum() == -500
                && slider->maximum() == 500) {
                m_exposureSlider = slider;
                break;
            }
        }

        if (m_exposureLabel && m_exposureSlider) {
            m_exposureLabel->installEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_exposureLabel.data()
            && event->type() == QEvent::MouseButtonDblClick) {
            auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton
                && mouseEvent->modifiers().testFlag(Qt::ShiftModifier)) {
                applyAutoExposure();
                return true;
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    void applyAutoExposure()
    {
        if (!m_canvas || !m_canvas->hasImage() || !m_exposureSlider) {
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

        const imaging::AutoExposureResult estimate =
            imaging::estimateAutoExposure(image);
        if (!estimate.valid) {
            return;
        }

        const int correctionHundredths =
            static_cast<int>(std::lround(estimate.deltaStops * 100.0));
        const int targetValue = std::clamp(
            m_exposureSlider->value() + correctionHundredths,
            m_exposureSlider->minimum(),
            m_exposureSlider->maximum());

        if (targetValue == m_exposureSlider->value()) {
            return;
        }

        const bool wasSliderDown = m_exposureSlider->isSliderDown();
        if (!wasSliderDown) {
            m_exposureSlider->setSliderDown(true);
        }
        m_exposureSlider->setValue(targetValue);
        if (!wasSliderDown) {
            m_exposureSlider->setSliderDown(false);
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
    QPointer<QLabel> m_exposureLabel;
    QPointer<QSlider> m_exposureSlider;
};
