#pragma once

#include "imaging/ColorMixerSettings.h"

#include <QString>
#include <QWidget>

#include <array>
#include <cstddef>

class QLabel;
class MainWindow;
class QSlider;
class QToolButton;

class ColorMixerPanel final : public QWidget
{
public:
    static ColorMixerPanel *install(MainWindow *window);

    explicit ColorMixerPanel(MainWindow *window, QWidget *parent = nullptr);

    void setSettings(const imaging::ColorMixerSettings &settings);
    [[nodiscard]] imaging::ColorMixerSettings settings() const;
    [[nodiscard]] bool isEditing() const;

private:
    enum class Mode {
        Hue = 0,
        Saturation,
        Luminance,
    };

    void setMode(Mode mode);
    void refreshSliders();
    void refreshEnabledState();
    void handleSliderValueChanged(QSlider *slider, QLabel *valueLabel);
    void handleSliderReleased(QSlider *slider);
    void handleEyeToggled(bool enabled);
    [[nodiscard]] std::array<int, imaging::kColorMixerBandCount> &valuesForMode(Mode mode);
    [[nodiscard]] const std::array<int, imaging::kColorMixerBandCount> &valuesForMode(Mode mode) const;
    [[nodiscard]] QString historyLabelForSlider(const QSlider *slider) const;

    MainWindow *m_window = nullptr;
    QWidget *m_content = nullptr;
    QToolButton *m_eyeButton = nullptr;
    std::array<QToolButton *, 3> m_modeButtons{};
    std::array<QSlider *, imaging::kColorMixerBandCount> m_sliders{};
    std::array<QLabel *, imaging::kColorMixerBandCount> m_valueLabels{};
    imaging::ColorMixerSettings m_settings;
    Mode m_mode = Mode::Hue;
    bool m_settingSettings = false;
};
