#pragma once

#include <QSlider>
#include <QWidget>

class BasicPanel final : public QWidget
{
public:
    explicit BasicPanel(QWidget *parent = nullptr);

    void setCameraTemperatureKelvin(int kelvin)
    {
        auto *temperatureSlider = findChild<QSlider *>(QStringLiteral("temperatureSlider"));
        if (temperatureSlider != nullptr) {
            temperatureSlider->setValue(kelvin > 0 ? kelvin : 6500);
        }
    }
};
