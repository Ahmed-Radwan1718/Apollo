#pragma once

#include <QMainWindow>
#include <QString>

class QAction;
class BasicPanel;
class HistogramWidget;
class NavigatorPanel;
class PhotoCanvas;
class PhotoMetadataWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);

private slots:
    void chooseAndOpenImage();
    void openImage(const QString &filePath);
    void applyTemperature(int kelvin);

private:
    void createActions();

    NavigatorPanel *m_navigator = nullptr;
    PhotoCanvas *m_canvas = nullptr;
    HistogramWidget *m_histogram = nullptr;
    PhotoMetadataWidget *m_photoMetadata = nullptr;
    BasicPanel *m_basicPanel = nullptr;

    QString m_currentFilePath;

    QAction *m_openAction = nullptr;
    QAction *m_fitAction = nullptr;
    QAction *m_actualPixelsAction = nullptr;
    QAction *m_zoomInAction = nullptr;
    QAction *m_zoomOutAction = nullptr;
};
