#pragma once

#include "app/DevelopState.h"
#include "app/HistoryModel.h"

#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QString>

#include <optional>

class QAction;
class BasicPanel;
class CalibrationPanel;
class ColorMixerPanel;
class CropToolController;
class DetailPanel;
class DevelopSettingsClipboard;
class DevelopSettingsSync;
class EffectsPanel;
class FilmstripWidget;
class HistogramWidget;
class HistoryPanel;
class NavigatorPanel;
class PhotoCanvas;
class PhotoFlipController;
class PhotoMetadataWidget;
class QSlider;
class TransformController;
class WhiteBalancePickerController;

namespace imaging {
class TemperatureRenderService;
class WhiteBalancePickerService;
}

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void chooseAndOpenImage();
    void openImage(const QString &filePath);
    void applyTemperature(int kelvin);
    void handleDevelopRender(const QString &filePath,
                             int temperatureKelvin,
                             int tint,
                             const imaging::RawCalibration &calibration,
                             int exposureHundredths,
                             int highlights,
                             int shadows,
                             int whites,
                             int blacks,
                             int contrast,
                             int texture,
                             int clarity,
                             int dehaze,
                             int saturation,
                             int vibrance,
                             const imaging::ColorMixerSettings &colorMixer,
                             bool preview,
                             const QImage &image,
                             const QString &error);
    void handleWhiteBalancePick(const QString &filePath,
                                int temperatureKelvin,
                                int tint,
                                const QString &error);
    void handleHistoryEntryActivated(int index);

private:
    friend class ColorMixerPanel;
    friend class CropToolController;
    friend class DetailPanel;
    friend class DevelopSettingsClipboard;
    friend class DevelopSettingsSync;
    friend class EffectsPanel;
    friend class PhotoFlipController;
    friend class TransformController;

    void createActions();
    void previewTemperature(int kelvin);
    void previewTint(int tint);
    void applyTint(int tint);
    void previewExposure(int exposureHundredths);
    void applyExposure(int exposureHundredths);
    void previewHighlights(int highlights);
    void applyHighlights(int highlights);
    void previewShadows(int shadows);
    void applyShadows(int shadows);
    void previewWhites(int whites);
    void applyWhites(int whites);
    void previewBlacks(int blacks);
    void applyBlacks(int blacks);
    void previewContrast(int contrast);
    void applyContrast(int contrast);
    void previewTexture(int texture);
    void applyTexture(int texture);
    void previewClarity(int clarity);
    void applyClarity(int clarity);
    void previewDehaze(int dehaze);
    void applyDehaze(int dehaze);
    void previewSaturation(int saturation);
    void applySaturation(int saturation);
    void previewVibrance(int vibrance);
    void applyVibrance(int vibrance);
    void requestPreview(const DevelopState &state);
    void requestFullRender(const DevelopState &state, const QString &historyLabel = {});
    void setDevelopControlsFromState(const DevelopState &state);
    void refreshHistoryPanel();

    NavigatorPanel *m_navigator = nullptr;
    HistoryPanel *m_historyPanel = nullptr;
    PhotoCanvas *m_canvas = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    HistogramWidget *m_histogram = nullptr;
    PhotoMetadataWidget *m_photoMetadata = nullptr;
    BasicPanel *m_basicPanel = nullptr;
    ColorMixerPanel *m_colorMixerPanel = nullptr;
    CalibrationPanel *m_calibrationPanel = nullptr;
    QSlider *m_temperatureSlider = nullptr;
    QSlider *m_tintSlider = nullptr;
    QSlider *m_exposureSlider = nullptr;
    QSlider *m_highlightsSlider = nullptr;
    QSlider *m_shadowsSlider = nullptr;
    QSlider *m_whitesSlider = nullptr;
    QSlider *m_blacksSlider = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QSlider *m_textureSlider = nullptr;
    QSlider *m_claritySlider = nullptr;
    QSlider *m_dehazeSlider = nullptr;
    QSlider *m_saturationSlider = nullptr;
    QSlider *m_vibranceSlider = nullptr;
    imaging::TemperatureRenderService *m_renderService = nullptr;
    imaging::WhiteBalancePickerService *m_whiteBalancePickerService = nullptr;
    WhiteBalancePickerController *m_whiteBalancePickerController = nullptr;

    QString m_currentFilePath;
    QImage m_originalImage;
    QSize m_fullImageSize;
    DevelopState m_requestedState;
    std::optional<DevelopState> m_fullQualityState;
    HistoryModel m_historyModel;
    bool m_settingDevelopControls = false;

    QAction *m_openAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualPixelsAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
};
