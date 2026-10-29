#pragma once

#include "ui/rightpanel/AutoExposureController.h"

#include <QEvent>
#include <QMouseEvent>
#include <QObject>
#include <QPointer>
#include <QSlider>
#include <QTimer>
#include <QWidget>

class BasicSliderInteractionController final : public QObject
{
public:
    explicit BasicSliderInteractionController(QWidget *basicPanel,
                                              QObject *parent = nullptr)
        : QObject(parent)
        , m_basicPanel(basicPanel)
    {
        if (!m_basicPanel) {
            return;
        }

        const QList<QSlider *> sliders = m_basicPanel->findChildren<QSlider *>();
        for (QSlider *slider : sliders) {
            if (slider) {
                slider->installEventFilter(this);
            }
        }

        new AutoExposureController(m_basicPanel.data(), this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        auto *slider = qobject_cast<QSlider *>(watched);
        if (!slider) {
            return QObject::eventFilter(watched, event);
        }

        if (event->type() == QEvent::MouseButtonPress) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton && !slider->isSliderDown()) {
                // Groove clicks can move QSlider without following the same
                // pressed/released lifecycle as a handle drag on every style.
                // Mark the interaction as active before QSlider processes the
                // click so Nexus previews it through the normal drag path.
                slider->setSliderDown(true);
            }
        } else if (event->type() == QEvent::MouseButtonRelease) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                QPointer<QSlider> guardedSlider(slider);
                QTimer::singleShot(0, slider, [guardedSlider]() {
                    if (guardedSlider && guardedSlider->isSliderDown()) {
                        // setSliderDown(false) emits sliderReleased(), which is
                        // the existing signal Nexus uses to commit a full render.
                        guardedSlider->setSliderDown(false);
                    }
                });
            }
        }

        return QObject::eventFilter(watched, event);
    }

private:
    QPointer<QWidget> m_basicPanel;
};
