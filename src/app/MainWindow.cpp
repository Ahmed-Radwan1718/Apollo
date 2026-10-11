#include "app/MainWindow.h"

#include "imaging/TemperatureRenderService.h"
#include "imaging/ImageLoader.h"
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
#include <QList>
#include <QMessageBox>
#include <QSlider>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTemperatureMinimumKelvin = 2000;
constexpr int kTemperatureMaximumKelvin = 20000;
constexpr int kDefaultTemperatureKelvin = 6500;

int temperatureKelvinFromSlider(const QSlider *slider, int position)
{
    if (!slider || slider->maximum() <= slider->minimum()) {
        return kDefaultTemperatureKelvin;
    }

    const double normalized = std::clamp(
        static_cast<double>(position - slider->minimum())
            / static_cast<double>(slider->maximum() - slider->minimum()),
        0.0,
        1.0);
    const double minimumReciprocal = 1.0 / static_cast<double>(kTemperatureMinimumKelvin);
    const double maximumReciprocal = 1.0 / static_cast<double>(kTemperatureMaximumKelvin);
    const double reciprocal = minimumReciprocal
        - normalized * (minimumReciprocal - maximumReciprocal);

    return std::clamp(static_cast<int>(std::lround(1.0 / reciprocal)),
                      kTemperatureMinimumKelvin,
                      kTemperatureMaximumKelvin);
}

QSlider *findExposureSlider(BasicPanel *panel)
{
    if (!panel) {
        return nullptr;
    }

    const QList<QSlider *> sliders = panel->findChildren<QSlider *>();
    for (QSlider *slider : sliders) {
        if (slider->minimum() == -500 && slider->maximum() == 500) {
            return slider;
        }
    }

    return nullptr;
}

} // namespace

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

    m_renderService = new imaging::TemperatureRenderService(this);
    m_exposureSlider = findExposureSlider(m_basicPanel);

    createActions();

    connect(m_canvas, &PhotoCanvas::fileDropped,
            this, &MainWindow::openImage);

    if (auto *temperatureSlider =
            m_basicPanel->findChild<QSlider *>(QStringLiteral("temperatureSlider"))) {
        connect(temperatureSlider, &QSlider::valueChanged, this,
                [this, temperatureSlider](int position) {
                    if (m_settingDevelopControls || !temperatureSlider->isSliderDown()) {
                        return;
                    }
                    previewTemperature(temperatureKelvinFromSlider(temperatureSlider, position));
                });
    }

    connect(m_basicPanel, &BasicPanel::temperatureChanged,
            this, &MainWindow::applyTemperature);

    if (m_exposureSlider) {
        connect(m_exposureSlider, &QSlider::valueChanged, this,
                [this](int exposureHundredths) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_exposureSlider->isSliderDown()) {
                        previewExposure(exposureHundredths);
                    } else {
                        applyExposure(exposureHundredths);
                    }
                });
        connect(m_exposureSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyExposure(m_exposureSlider->value());
                    }
                });
    }

    connect(m_renderService, &imaging::TemperatureRenderService::renderReady,
            this, &MainWindow::handleDevelopRender);
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
    m_requestedState.temperatureKelvin = result.metadata.whiteBalanceTemperatureKelvin > 0
        ? result.metadata.whiteBalanceTemperatureKelvin
        : kDefaultTemperatureKelvin;
    m_requestedState.exposureHundredths = 0;
    m_fullQualityState = m_requestedState;

    m_canvas->setImage(result.image);
    m_navigator->setImage(result.image);
    m_histogram->setImage(result.image);
    m_photoMetadata->setMetadata(result.metadata);

    m_settingDevelopControls = true;
    m_basicPanel->setCameraTemperatureKelvin(result.metadata.whiteBalanceTemperatureKelvin);
    if (m_exposureSlider) {
        m_exposureSlider->setValue(0);
    }
    m_settingDevelopControls = false;

    setWindowTitle(QStringLiteral("%1 — Nexus").arg(QFileInfo(filePath).fileName()));
}

void MainWindow::previewTemperature(int kelvin)
{
    DevelopState state = m_requestedState;
    state.temperatureKelvin = kelvin;
    requestPreview(state);
}

void MainWindow::applyTemperature(int kelvin)
{
    DevelopState state = m_requestedState;
    state.temperatureKelvin = kelvin;
    requestFullRender(state);
}

void MainWindow::previewExposure(int exposureHundredths)
{
    DevelopState state = m_requestedState;
    state.exposureHundredths = exposureHundredths;
    requestPreview(state);
}

void MainWindow::applyExposure(int exposureHundredths)
{
    DevelopState state = m_requestedState;
    state.exposureHundredths = exposureHundredths;
    requestFullRender(state);
}

void MainWindow::requestPreview(const DevelopState &state)
{
    if (m_currentFilePath.isEmpty()
        || QFileInfo(m_currentFilePath).suffix().compare(QStringLiteral("arw"),
                                                        Qt::CaseInsensitive) != 0) {
        return;
    }

    m_requestedState = state;
    m_fullQualityState.reset();
    m_renderService->requestPreview(m_currentFilePath,
                                    state.temperatureKelvin,
                                    state.exposureHundredths);
}

void MainWindow::requestFullRender(const DevelopState &state)
{
    if (m_currentFilePath.isEmpty()
        || QFileInfo(m_currentFilePath).suffix().compare(QStringLiteral("arw"),
                                                        Qt::CaseInsensitive) != 0) {
        return;
    }

    m_requestedState = state;
    m_renderService->requestFullRender(m_currentFilePath,
                                       state.temperatureKelvin,
                                       state.exposureHundredths);
}

void MainWindow::handleDevelopRender(const QString &filePath,
                                     int temperatureKelvin,
                                     int exposureHundredths,
                                     bool preview,
                                     const QImage &image,
                                     const QString &error)
{
    const DevelopState renderedState{
        temperatureKelvin,
        exposureHundredths,
    };

    if (filePath != m_currentFilePath || renderedState != m_requestedState) {
        return;
    }

    if (image.isNull()) {
        if (!preview && !error.isEmpty()) {
            QMessageBox::critical(
                this,
                tr("Could not render adjustments"),
                tr("Nexus could not render the requested RAW adjustments:\n%1\n\n%2")
                    .arg(QDir::toNativeSeparators(m_currentFilePath), error));
        }
        return;
    }

    if (preview) {
        if (m_fullQualityState && *m_fullQualityState == renderedState) {
            return;
        }
        m_canvas->replacePreviewImage(image, m_fullImageSize);
    } else {
        m_canvas->replaceImage(image);
        m_fullImageSize = image.size();
        m_fullQualityState = renderedState;
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
