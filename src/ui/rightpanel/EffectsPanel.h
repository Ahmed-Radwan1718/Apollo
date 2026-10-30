#pragma once

#include "imaging/EffectsSettings.h"

#include <QString>
#include <QWidget>

struct DevelopState;
class MainWindow;
class QFrame;
class QImage;
class QSlider;
class QVBoxLayout;

class EffectsPanel final : public QWidget
{
public:
    static EffectsPanel *install(MainWindow *window);

    explicit EffectsPanel(MainWindow *window, QWidget *parent = nullptr);

    void setSettings(const imaging::EffectsSettings &settings);
    [[nodiscard]] imaging::EffectsSettings settings() const;
    [[nodiscard]] bool isEditing() const;

private:
    QSlider *addSlider(QFrame *content,
                       QVBoxLayout *layout,
                       const QString &label,
                       const QString &historyName,
                       int minimum,
                       int maximum,
                       int defaultValue,
                       bool signedValue = false);
    QString valueText(const QSlider *slider) const;
    QString historyLabelForSlider(const QSlider *slider) const;
    void handleSliderValueChanged(QSlider *slider);
    void handleSliderReleased(QSlider *slider);
    void syncFromWindowState();
    void applyRenderedEffects(const QString &filePath,
                              const DevelopState &renderedState,
                              bool preview,
                              const QImage &image);

    MainWindow *m_window = nullptr;
    QSlider *m_vignetteAmountSlider = nullptr;
    QSlider *m_vignetteMidpointSlider = nullptr;
    QSlider *m_vignetteRoundnessSlider = nullptr;
    QSlider *m_vignetteFeatherSlider = nullptr;
    QSlider *m_vignetteHighlightsSlider = nullptr;
    QSlider *m_grainAmountSlider = nullptr;
    QSlider *m_grainSizeSlider = nullptr;
    QSlider *m_grainRoughnessSlider = nullptr;
    bool m_settingSettings = false;
};
