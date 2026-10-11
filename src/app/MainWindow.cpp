#include "app/MainWindow.h"

#include "imaging/ImageLoader.h"
#include "imaging/TemperatureRenderService.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/histogram/HistogramWidget.h"
#include "ui/leftpanel/NavigatorPanel.h"
#include "ui/rightpanel/BasicPanel.h"
#include "ui/rightpanel/PhotoMetadataWidget.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    auto *workspace = new QWidget(this);
    auto *workspaceLayout = new QHBoxLayout(workspace);
    workspaceLayout->setContentsMargins(0, 0, 0, 0);
    workspaceLayout->setSpacing(0);

    auto *leftPanel = new QWidget(workspace);
    leftPanel->setObjectName(QStringLiteral("leftPanel"));
    leftPanel->setFixedWidth(260);

    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    leftLayout->setSpacing(0);

    m_navigator = new NavigatorPanel(leftPanel);
    leftLayout->addWidget(m_navigator);
    leftLayout->addStretch(1);

    workspaceLayout->addWidget(leftPanel);

    m_canvas = new PhotoCanvas(workspace);
    workspaceLayout->addWidget(m_canvas, 1);

    auto *rightPanel = new QWidget(workspace);
    rightPanel->setObjectName(QStringLiteral("rightPanel"));
    rightPanel->setFixedWidth(320);

    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(14, 14, 14, 0);
    rightLayout->setSpacing(8);

    auto *histogramTitle = new QLabel(tr("Histogram"), rightPanel);
    histogramTitle->setObjectName(QStringLiteral("histogramTitle"));
    rightLayout->addWidget(histogramTitle);

    m_histogram = new HistogramWidget(rightPanel);
    rightLayout->addWidget(m_histogram);

    m_photoMetadata = new PhotoMetadataWidget(rightPanel);
    rightLayout->addWidget(m_photoMetadata);

    m_basicPanel = new BasicPanel(rightPanel);
    rightLayout->addWidget(m_basicPanel);
    rightLayout->addStretch(1);

    workspaceLayout->addWidget(rightPanel);
    setCentralWidget(workspace);

    resize(1440, 900);
    setMinimumSize(800, 520);
    setWindowTitle(QStringLiteral("Nexus"));

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #181818; }"
        "QWidget#leftPanel {"
        "  background: #181818;"
        "  border-right: 1px solid #252525;"
        "}"
        "QWidget#rightPanel {"
        "  background: #181818;"
        "  border-left: 1px solid #252525;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#histogramTitle {"
        "  color: #b9b9b9;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 1px 2px 0 2px;"
        "}"));

    m_temperatureRenderer = new imaging::TemperatureRenderService(this);

    createActions();

    connect(m_canvas, &PhotoCanvas::fileDropped,
            this, &MainWindow::openImage);

    if (auto *temperatureSlider =
            m_basicPanel->findChild<QSlider *>(QStringLiteral("temperatureSlider"))) {
        // The value label and preview request both update on every slider position.
        // Rendering itself happens off the UI thread, so this connection stays cheap.
        connect(temperatureSlider, &QSlider::valueChanged,
                this, &MainWindow::previewTemperature);
    }

    // BasicPanel emits temperatureChanged when a drag is released (or when a
    // non-drag change such as keyboard input is committed). That requests the
    // full-quality render without blocking the UI.
    connect(m_basicPanel, &BasicPanel::temperatureChanged,
            this, &MainWindow::applyTemperature);

    connect(m_temperatureRenderer, &imaging::TemperatureRenderService::renderReady,
            this, &MainWindow::handleTemperatureRender);
}

void MainWindow::chooseAndOpenImage()
{
    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Open Photo"),
        {},
        imaging::ImageLoader::fileDialogFilter());

    if (!filePath.isEmpty()) {
        openImage(filePath);
    }
}

void MainWindow::openImage(const QString &filePath)
{
    const imaging::ImageLoadResult result = imaging::ImageLoader::load(filePath);
    if (!result.succeeded()) {
        QMessageBox::critical(
            this,
            tr("Could not open photo"),
            tr("Nexus could not decode:\n%1\n\n%2")
                .arg(QDir::toNativeSeparators(filePath), result.error));
        return;
    }

    m_currentFilePath = QFileInfo(filePath).absoluteFilePath();
    m_fullImageSize = result.image.size();
    m_requestedTemperatureKelvin = result.metadata.whiteBalanceTemperatureKelvin > 0
        ? result.metadata.whiteBalanceTemperatureKelvin
        : 6500;
    m_fullQualityTemperatureKelvin = m_requestedTemperatureKelvin;

    m_canvas->setImage(result.image);
    m_navigator->setImage(result.image);
    m_histogram->setImage(result.image);
    m_photoMetadata->setMetadata(result.metadata);

    m_settingCameraTemperature = true;
    m_basicPanel->setCameraTemperatureKelvin(result.metadata.whiteBalanceTemperatureKelvin);
    m_settingCameraTemperature = false;

    setWindowTitle(QStringLiteral("%1 — Nexus").arg(QFileInfo(filePath).fileName()));
}

void MainWindow::previewTemperature(int kelvin)
{
    if (m_settingCameraTemperature
        || m_currentFilePath.isEmpty()
        || QFileInfo(m_currentFilePath).suffix().compare(QStringLiteral("arw"),
                                                        Qt::CaseInsensitive) != 0) {
        return;
    }

    m_requestedTemperatureKelvin = kelvin;
    m_fullQualityTemperatureKelvin = 0;
    m_temperatureRenderer->requestPreview(m_currentFilePath, kelvin);
}

void MainWindow::applyTemperature(int kelvin)
{
    if (m_currentFilePath.isEmpty()
        || QFileInfo(m_currentFilePath).suffix().compare(QStringLiteral("arw"),
                                                        Qt::CaseInsensitive) != 0) {
        return;
    }

    m_requestedTemperatureKelvin = kelvin;
    m_temperatureRenderer->requestFullRender(m_currentFilePath, kelvin);
}

void MainWindow::handleTemperatureRender(const QString &filePath,
                                         int kelvin,
                                         bool preview,
                                         const QImage &image,
                                         const QString &error)
{
    // A worker may finish after the user has already scrubbed somewhere else or
    // opened another photo. Ignore that stale result instead of flashing backwards.
    if (filePath != m_currentFilePath || kelvin != m_requestedTemperatureKelvin) {
        return;
    }

    if (image.isNull()) {
        // Preview failures should never interrupt a drag with modal UI. If the
        // corresponding final render also fails, report that once on commit.
        if (!preview && !error.isEmpty()) {
            QMessageBox::critical(
                this,
                tr("Could not adjust temperature"),
                tr("Nexus could not render the requested white balance:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(m_currentFilePath), error));
        }
        return;
    }

    if (preview) {
        // If the full render for this exact Kelvin has already landed, never let
        // a slower stale half-size preview replace it afterward.
        if (m_fullQualityTemperatureKelvin == kelvin) {
            return;
        }
        m_canvas->replacePreviewImage(image, m_fullImageSize);
    } else {
        m_canvas->replaceImage(image);
        m_fullImageSize = image.size();
        m_fullQualityTemperatureKelvin = kelvin;
    }

    m_navigator->setImage(image);
    m_histogram->setImage(image);
}

void MainWindow::createActions()
{
    m_openAction = new QAction(tr("Open Photo"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered,
            this, &MainWindow::chooseAndOpenImage);
    addAction(m_openAction);

    m_fitAction = new QAction(tr("Fit to View"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(m_fitAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::fitToView);
    addAction(m_fitAction);

    m_actualPixelsAction = new QAction(tr("100% (Actual Pixels)"), this);
    m_actualPixelsAction->setShortcut(QKeySequence(Qt::Key_1));
    connect(m_actualPixelsAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::actualPixels);
    addAction(m_actualPixelsAction);

    m_zoomInAction = new QAction(tr("Zoom In"), this);
    m_zoomInAction->setShortcuts(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::zoomIn);
    addAction(m_zoomInAction);

    m_zoomOutAction = new QAction(tr("Zoom Out"), this);
    m_zoomOutAction->setShortcuts(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::zoomOut);
    addAction(m_zoomOutAction);
}
