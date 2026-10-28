#pragma once

#include "imaging/RawCalibration.h"

#include <QString>
#include <QWidget>

class QFrame;
class QSlider;
class QToolButton;
class QVBoxLayout;

class CalibrationPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit CalibrationPanel(QWidget *parent = nullptr);

    void setCalibration(const imaging::RawCalibration &calibration);
    [[nodiscard]] imaging::RawCalibration calibration() const;
    [[nodiscard]] bool isEditing() const;

signals:
    void previewRequested(const imaging::RawCalibration &calibration);
    void commitRequested(const imaging::RawCalibration &calibration,
                         const QString &historyLabel);

private:
    QSlider *addSlider(QFrame *content,
                       QVBoxLayout *layout,
                       const QString &label,
                       const QString &historyName);
    QString historyLabelForSlider(const QSlider *slider) const;
    void handleSliderValueChanged(QSlider *slider);
    void handleSliderReleased(QSlider *slider);

    QFrame *m_content = nullptr;
    QToolButton *m_chevronButton = nullptr;
    QSlider *m_shadowsTintSlider = nullptr;
    QSlider *m_redHueSlider = nullptr;
    QSlider *m_redSaturationSlider = nullptr;
    QSlider *m_greenHueSlider = nullptr;
    QSlider *m_greenSaturationSlider = nullptr;
    QSlider *m_blueHueSlider = nullptr;
    QSlider *m_blueSaturationSlider = nullptr;
    bool m_settingCalibration = false;
};
