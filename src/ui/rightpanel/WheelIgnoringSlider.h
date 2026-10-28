#pragma once

#include <QSlider>
#include <QWheelEvent>

class WheelIgnoringSlider : public QSlider
{
public:
    explicit WheelIgnoringSlider(Qt::Orientation orientation,
                                 QWidget *parent = nullptr)
        : QSlider(orientation, parent)
    {
    }

protected:
    void wheelEvent(QWheelEvent *event) override
    {
        event->ignore();
    }
};
