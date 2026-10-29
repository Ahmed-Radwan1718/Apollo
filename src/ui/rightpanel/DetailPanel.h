#pragma once

#include "imaging/DetailSettings.h"

#include <QString>
#include <QWidget>

class MainWindow;
class QFrame;
class QSlider;
class QVBoxLayout;

class DetailPanel final : public QWidget
{
public:
    static DetailPanel *install(MainWindow *window);

    explicit DetailPanel(MainWindow *window, QWidget *parent = nullptr);

    void setSettings(const imaging::DetailSettings &settings);
    [[nodiscard]] imaging::DetailSettings settings() const;
    [[nodiscard]] bool isEditing() const;

private:
    QSlider *addSlider(QFrame *content,
                       QVBoxLayout *layout,
                       const QString &label,
                       const QString &historyName,
                       int minimum,
                       int maximum,
                       int defaultValue,
                       bool radiusFormat = false);
    QString valueText(const QSlider *slider) const;
    QString historyLabelForSlider(const QSlider *slider) const;
    void handleSliderValueChanged(QSlider *slider);
    void handleSliderReleased(QSlider *slider);
    void syncFromWindowState();

    MainWindow *m_window = nullptr;
    QSlider *m_sharpeningAmountSlider = nullptr;
    QSlider *m_sharpeningRadiusSlider = nullptr;
    QSlider *m_sharpeningDetailSlider = nullptr;
    QSlider *m_sharpeningMaskingSlider = nullptr;
    QSlider *m_luminanceNoiseReductionSlider = nullptr;
    QSlider *m_colorNoiseReductionSlider = nullptr;
    bool m_settingSettings = false;
};
