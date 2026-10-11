#pragma once

#include <QWidget>

class QSlider;

class BasicPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit BasicPanel(QWidget *parent = nullptr);

    void setCameraTemperatureKelvin(int kelvin);

signals:
    void temperatureChanged(int kelvin);

private:
    QSlider *m_temperatureSlider = nullptr;
    bool m_settingCameraTemperature = false;
};
