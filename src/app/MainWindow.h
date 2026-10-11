#pragma once

#include <QImage>
#include <QMainWindow>
#include <QSize>
#include <QString>

class QAction;
class BasicPanel;
class HistogramWidget;
class NavigatorPanel;
class PhotoCanvas;
class PhotoMetadataWidget;

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
    void previewTemperature(int kelvin);
    void applyTemperature(int kelvin);
    void handleTemperatureRender(const QString &filePath,
                                 int kelvin,
                                 bool preview,
                                 const QImage &image,
                                 const QString &error);

private:
    void createActions();

    NavigatorPanel *m_navigator = nullptr;
    PhotoCanvas *m_canvas = nullptr;
    HistogramWidget *m_histogram = nullptr;
    PhotoMetadataWidget *m_photoMetadata = nullptr;
    BasicPanel *m_basicPanel = nullptr;
    imaging::TemperatureRenderService *m_temperatureRenderer = nullptr;

    QString m_currentFilePath;
    QSize m_fullImageSize;
    int m_requestedTemperatureKelvin = 0;
    int m_fullQualityTemperatureKelvin = 0;
    bool m_settingCameraTemperature = false;

    QAction *m_openAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualPixelsAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
};
