#pragma once

#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QString>

#include <optional>

class QAction;
class BasicPanel;
class HistogramWidget;
class NavigatorPanel;
class PhotoCanvas;
class PhotoMetadataWidget;
class QSlider;

namespace imaging {
class TemperatureRenderService;
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
                             int exposureHundredths,
                             bool preview,
                             const QImage &image,
                             const QString &error);

private:
    struct DevelopState {
        int temperatureKelvin = 0;
        int exposureHundredths = 0;

        bool operator==(const DevelopState &) const = default;
    };

    void createActions();
    void previewTemperature(int kelvin);
    void previewExposure(int exposureHundredths);
    void applyExposure(int exposureHundredths);
    void requestPreview(const DevelopState &state);
    void requestFullRender(const DevelopState &state);

    NavigatorPanel *m_navigator = nullptr;
    PhotoCanvas *m_canvas = nullptr;
    HistogramWidget *m_histogram = nullptr;
    PhotoMetadataWidget *m_photoMetadata = nullptr;
    BasicPanel *m_basicPanel = nullptr;
    QSlider *m_exposureSlider = nullptr;
    imaging::TemperatureRenderService *m_renderService = nullptr;

    QString m_currentFilePath;
    QSize m_fullImageSize;
    DevelopState m_requestedState;
    std::optional<DevelopState> m_fullQualityState;
    bool m_settingDevelopControls = false;

    QAction *m_openAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualPixelsAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
};
