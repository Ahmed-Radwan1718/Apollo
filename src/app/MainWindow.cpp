#include "app/MainWindow.h"

#include "imaging/ImageLoader.h"
#include "imaging/TemperatureRenderService.h"
#include "imaging/WhiteBalancePickerService.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/filmstrip/FilmstripWidget.h"
#include "ui/histogram/HistogramWidget.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/leftpanel/NavigatorPanel.h"
#include "ui/rightpanel/BasicPanel.h"
#include "ui/rightpanel/PhotoMetadataWidget.h"
#include "ui/rightpanel/WhiteBalancePickerController.h"

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
#include <QStringList>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kTemperatureMinimumKelvin = 2000;
constexpr int kTemperatureMaximumKelvin = 20000;
constexpr int kDefaultTemperatureKelvin = 6500;

bool isArwPath(const QString &filePath)
{
    return !filePath.isEmpty()
        && QFileInfo(filePath).suffix().compare(QStringLiteral("arw"),
                                               Qt::CaseInsensitive) == 0;
}

QString signedHistoryValue(int value)
{
    return value > 0
        ? QStringLiteral("+%1").arg(value)
        : QString::number(value);
}

QString exposureHistoryValue(int hundredths)
{
    const double stops = static_cast<double>(hundredths) / 100.0;
    return QStringLiteral("%1%2")
        .arg(stops >= 0.0 ? QStringLiteral("+") : QString(),
             QString::number(stops, 'f', 2));
}

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

int temperatureSliderPositionForKelvin(const QSlider *slider, int kelvin)
{
    if (!slider || slider->maximum() <= slider->minimum()) {
        return 0;
    }

    const int clampedKelvin = std::clamp(kelvin,
                                         kTemperatureMinimumKelvin,
                                         kTemperatureMaximumKelvin);
    const double minimumReciprocal = 1.0 / static_cast<double>(kTemperatureMinimumKelvin);
    const double maximumReciprocal = 1.0 / static_cast<double>(kTemperatureMaximumKelvin);
    const double kelvinReciprocal = 1.0 / static_cast<double>(clampedKelvin);
    const double normalized = std::clamp(
        (minimumReciprocal - kelvinReciprocal)
            / (minimumReciprocal - maximumReciprocal),
        0.0,
        1.0);

    return slider->minimum()
        + static_cast<int>(std::lround(
            normalized * static_cast<double>(slider->maximum() - slider->minimum())));
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

QSlider *findLabeledSlider(BasicPanel *panel, const QString &labelText)
{
    if (!panel) {
        return nullptr;
    }

    const QList<QLabel *> labels = panel->findChildren<QLabel *>();
    for (QLabel *label : labels) {
        if (label->objectName() != QStringLiteral("basicControlLabel")
            || label->text() != labelText) {
            continue;
        }

        if (QWidget *control = label->parentWidget()) {
            return control->findChild<QSlider *>();
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

    m_historyPanel = new HistoryPanel(leftPanel);
    leftLayout->addWidget(m_historyPanel, 1);

    workspaceLayout->addWidget(leftPanel);

    auto *centerPanel = new QWidget(workspace);
    centerPanel->setObjectName(QStringLiteral("centerPanel"));
    auto *centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    centerLayout->setSpacing(0);

    m_canvas = new PhotoCanvas(centerPanel);
    centerLayout->addWidget(m_canvas, 1);

    m_filmstrip = new FilmstripWidget(centerPanel);
    centerLayout->addWidget(m_filmstrip);

    workspaceLayout->addWidget(centerPanel, 1);

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
    m_whiteBalancePickerService = new imaging::WhiteBalancePickerService(this);
    m_temperatureSlider =
        m_basicPanel->findChild<QSlider *>(QStringLiteral("temperatureSlider"));
    m_tintSlider = m_basicPanel->findChild<QSlider *>(QStringLiteral("tintSlider"));
    m_exposureSlider = findExposureSlider(m_basicPanel);
    m_highlightsSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Highlights"));
    m_shadowsSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Shadows"));
    m_whitesSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Whites"));
    m_contrastSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Contrast"));
    m_textureSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Texture"));
    m_dehazeSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Dehaze"));
    m_saturationSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Saturation"));
    m_vibranceSlider = findLabeledSlider(m_basicPanel, QStringLiteral("Vibrance"));
    m_whiteBalancePickerController =
        new WhiteBalancePickerController(m_basicPanel, m_canvas, this);

    createActions();

    connect(m_canvas, &PhotoCanvas::fileDropped,
            this, &MainWindow::openImage);
    connect(m_historyPanel, &HistoryPanel::entryActivated,
            this, &MainWindow::handleHistoryEntryActivated);

    if (m_temperatureSlider) {
        connect(m_temperatureSlider, &QSlider::valueChanged, this,
                [this](int position) {
                    if (m_settingDevelopControls || !m_temperatureSlider->isSliderDown()) {
                        return;
                    }
                    previewTemperature(
                        temperatureKelvinFromSlider(m_temperatureSlider, position));
                });
    }

    connect(m_basicPanel, &BasicPanel::temperatureChanged, this,
            [this](int kelvin) {
                if (!m_settingDevelopControls) {
                    applyTemperature(kelvin);
                }
            });

    if (m_tintSlider) {
        connect(m_tintSlider, &QSlider::valueChanged, this,
                [this](int tint) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_tintSlider->isSliderDown()) {
                        previewTint(tint);
                    } else {
                        applyTint(tint);
                    }
                });
        connect(m_tintSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyTint(m_tintSlider->value());
                    }
                });
    }

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

    if (m_highlightsSlider) {
        connect(m_highlightsSlider, &QSlider::valueChanged, this,
                [this](int highlights) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_highlightsSlider->isSliderDown()) {
                        previewHighlights(highlights);
                    } else {
                        applyHighlights(highlights);
                    }
                });
        connect(m_highlightsSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyHighlights(m_highlightsSlider->value());
                    }
                });
    }

    if (m_shadowsSlider) {
        connect(m_shadowsSlider, &QSlider::valueChanged, this,
                [this](int shadows) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_shadowsSlider->isSliderDown()) {
                        previewShadows(shadows);
                    } else {
                        applyShadows(shadows);
                    }
                });
        connect(m_shadowsSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyShadows(m_shadowsSlider->value());
                    }
                });
    }

    if (m_whitesSlider) {
        connect(m_whitesSlider, &QSlider::valueChanged, this,
                [this](int whites) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_whitesSlider->isSliderDown()) {
                        previewWhites(whites);
                    } else {
                        applyWhites(whites);
                    }
                });
        connect(m_whitesSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyWhites(m_whitesSlider->value());
                    }
                });
    }

    if (m_contrastSlider) {
        connect(m_contrastSlider, &QSlider::valueChanged, this,
                [this](int contrast) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_contrastSlider->isSliderDown()) {
                        previewContrast(contrast);
                    } else {
                        applyContrast(contrast);
                    }
                });
        connect(m_contrastSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyContrast(m_contrastSlider->value());
                    }
                });
    }

    if (m_textureSlider) {
        connect(m_textureSlider, &QSlider::valueChanged, this,
                [this](int texture) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_textureSlider->isSliderDown()) {
                        previewTexture(texture);
                    } else {
                        applyTexture(texture);
                    }
                });
        connect(m_textureSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyTexture(m_textureSlider->value());
                    }
                });
    }

    if (m_dehazeSlider) {
        connect(m_dehazeSlider, &QSlider::valueChanged, this,
                [this](int dehaze) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_dehazeSlider->isSliderDown()) {
                        previewDehaze(dehaze);
                    } else {
                        applyDehaze(dehaze);
                    }
                });
        connect(m_dehazeSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyDehaze(m_dehazeSlider->value());
                    }
                });
    }

    if (m_saturationSlider) {
        connect(m_saturationSlider, &QSlider::valueChanged, this,
                [this](int saturation) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_saturationSlider->isSliderDown()) {
                        previewSaturation(saturation);
                    } else {
                        applySaturation(saturation);
                    }
                });
        connect(m_saturationSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applySaturation(m_saturationSlider->value());
                    }
                });
    }

    if (m_vibranceSlider) {
        connect(m_vibranceSlider, &QSlider::valueChanged, this,
                [this](int vibrance) {
                    if (m_settingDevelopControls) {
                        return;
                    }

                    if (m_vibranceSlider->isSliderDown()) {
                        previewVibrance(vibrance);
                    } else {
                        applyVibrance(vibrance);
                    }
                });
        connect(m_vibranceSlider, &QSlider::sliderReleased, this,
                [this]() {
                    if (!m_settingDevelopControls) {
                        applyVibrance(m_vibranceSlider->value());
                    }
                });
    }

    connect(m_whiteBalancePickerController,
            &WhiteBalancePickerController::pointPicked,
            this,
            [this](const QPointF &normalizedPosition) {
                if (isArwPath(m_currentFilePath)) {
                    m_whiteBalancePickerService->request(m_currentFilePath,
                                                         normalizedPosition);
                }
            });

    connect(m_whiteBalancePickerService,
            &imaging::WhiteBalancePickerService::resultReady,
            this,
            &MainWindow::handleWhiteBalancePick);

    connect(m_renderService, &imaging::TemperatureRenderService::renderReady,
            this, &MainWindow::handleDevelopRender);
}

void MainWindow::chooseAndOpenImage()
{
    const QStringList filePaths = QFileDialog::getOpenFileNames(
        this,
        tr("Open Photos"),
        {},
        imaging::ImageLoader::fileDialogFilter());

    for (const QString &filePath : filePaths) {
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

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    DevelopState originalState;
    originalState.temperatureKelvin = result.metadata.whiteBalanceTemperatureKelvin > 0
        ? result.metadata.whiteBalanceTemperatureKelvin
        : kDefaultTemperatureKelvin;

    m_historyModel.initializePhoto(absoluteFilePath, originalState);
    const std::optional<DevelopState> savedState =
        m_historyModel.currentState(absoluteFilePath);

    m_currentFilePath = absoluteFilePath;
    m_originalImage = result.image;
    m_fullImageSize = result.image.size();
    m_requestedState = savedState.value_or(originalState);

    const bool showingOriginal = m_historyModel.currentIndex(m_currentFilePath) == 0;
    m_fullQualityState = showingOriginal
        ? std::optional<DevelopState>(m_requestedState)
        : std::nullopt;

    m_whiteBalancePickerController->setActive(false);
    m_whiteBalancePickerController->setEnabled(isArwPath(m_currentFilePath));

    m_canvas->setImage(result.image);
    m_navigator->setImage(result.image);
    m_histogram->setImage(result.image);
    m_photoMetadata->setMetadata(result.metadata);
    m_filmstrip->addPhoto(m_currentFilePath, result.image);

    m_basicPanel->setCameraTemperatureKelvin(result.metadata.whiteBalanceTemperatureKelvin);
    setDevelopControlsFromState(m_requestedState);
    refreshHistoryPanel();

    setWindowTitle(QStringLiteral("%1 — Nexus").arg(QFileInfo(filePath).fileName()));

    if (!showingOriginal && isArwPath(m_currentFilePath)) {
        requestFullRender(m_requestedState);
    }
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
    requestFullRender(state,
                      tr("Temperature %1").arg(kelvin));
}

void MainWindow::previewTint(int tint)
{
    DevelopState state = m_requestedState;
    state.tint = tint;
    requestPreview(state);
}

void MainWindow::applyTint(int tint)
{
    DevelopState state = m_requestedState;
    state.tint = tint;
    requestFullRender(state,
                      tr("Tint %1").arg(signedHistoryValue(tint)));
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
    requestFullRender(state,
                      tr("Exposure %1").arg(exposureHistoryValue(exposureHundredths)));
}

void MainWindow::previewHighlights(int highlights)
{
    DevelopState state = m_requestedState;
    state.highlights = highlights;
    requestPreview(state);
}

void MainWindow::applyHighlights(int highlights)
{
    DevelopState state = m_requestedState;
    state.highlights = highlights;
    requestFullRender(state,
                      tr("Highlights %1").arg(signedHistoryValue(highlights)));
}

void MainWindow::previewShadows(int shadows)
{
    DevelopState state = m_requestedState;
    state.shadows = shadows;
    requestPreview(state);
}

void MainWindow::applyShadows(int shadows)
{
    DevelopState state = m_requestedState;
    state.shadows = shadows;
    requestFullRender(state,
                      tr("Shadows %1").arg(signedHistoryValue(shadows)));
}

void MainWindow::previewWhites(int whites)
{
    DevelopState state = m_requestedState;
    state.whites = whites;
    requestPreview(state);
}

void MainWindow::applyWhites(int whites)
{
    DevelopState state = m_requestedState;
    state.whites = whites;
    requestFullRender(state,
                      tr("Whites %1").arg(signedHistoryValue(whites)));
}

void MainWindow::previewContrast(int contrast)
{
    DevelopState state = m_requestedState;
    state.contrast = contrast;
    requestPreview(state);
}

void MainWindow::applyContrast(int contrast)
{
    DevelopState state = m_requestedState;
    state.contrast = contrast;
    requestFullRender(state,
                      tr("Contrast %1").arg(signedHistoryValue(contrast)));
}

void MainWindow::previewTexture(int texture)
{
    DevelopState state = m_requestedState;
    state.texture = texture;
    requestPreview(state);
}

void MainWindow::applyTexture(int texture)
{
    DevelopState state = m_requestedState;
    state.texture = texture;
    requestFullRender(state,
                      tr("Texture %1").arg(signedHistoryValue(texture)));
}

void MainWindow::previewDehaze(int dehaze)
{
    DevelopState state = m_requestedState;
    state.dehaze = dehaze;
    requestPreview(state);
}

void MainWindow::applyDehaze(int dehaze)
{
    DevelopState state = m_requestedState;
    state.dehaze = dehaze;
    requestFullRender(state,
                      tr("Dehaze %1").arg(signedHistoryValue(dehaze)));
}

void MainWindow::previewSaturation(int saturation)
{
    DevelopState state = m_requestedState;
    state.saturation = saturation;
    requestPreview(state);
}

void MainWindow::applySaturation(int saturation)
{
    DevelopState state = m_requestedState;
    state.saturation = saturation;
    requestFullRender(state,
                      tr("Saturation %1").arg(signedHistoryValue(saturation)));
}

void MainWindow::previewVibrance(int vibrance)
{
    DevelopState state = m_requestedState;
    state.vibrance = vibrance;
    requestPreview(state);
}

void MainWindow::applyVibrance(int vibrance)
{
    DevelopState state = m_requestedState;
    state.vibrance = vibrance;
    requestFullRender(state,
                      tr("Vibrance %1").arg(signedHistoryValue(vibrance)));
}

void MainWindow::requestPreview(const DevelopState &state)
{
    if (!isArwPath(m_currentFilePath)) {
        return;
    }

    m_requestedState = state;
    m_fullQualityState.reset();
    m_renderService->requestPreview(m_currentFilePath,
                                    state.temperatureKelvin,
                                    state.tint,
                                    state.exposureHundredths,
                                    state.highlights,
                                    state.shadows,
                                    state.whites,
                                    state.contrast,
                                    state.texture,
                                    state.dehaze,
                                    state.saturation,
                                    state.vibrance);
}

void MainWindow::requestFullRender(const DevelopState &state, const QString &historyLabel)
{
    if (!isArwPath(m_currentFilePath)) {
        return;
    }

    m_requestedState = state;

    if (!historyLabel.isEmpty()
        && m_historyModel.commit(m_currentFilePath, historyLabel, state)) {
        refreshHistoryPanel();
    }

    m_renderService->requestFullRender(m_currentFilePath,
                                       state.temperatureKelvin,
                                       state.tint,
                                       state.exposureHundredths,
                                       state.highlights,
                                       state.shadows,
                                       state.whites,
                                       state.contrast,
                                       state.texture,
                                       state.dehaze,
                                       state.saturation,
                                       state.vibrance);
}

void MainWindow::handleDevelopRender(const QString &filePath,
                                     int temperatureKelvin,
                                     int tint,
                                     int exposureHundredths,
                                     int highlights,
                                     int shadows,
                                     int whites,
                                     int contrast,
                                     int texture,
                                     int dehaze,
                                     int saturation,
                                     int vibrance,
                                     bool preview,
                                     const QImage &image,
                                     const QString &error)
{
    const DevelopState renderedState{
        temperatureKelvin,
        tint,
        exposureHundredths,
        highlights,
        shadows,
        whites,
        contrast,
        texture,
        dehaze,
        saturation,
        vibrance,
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

void MainWindow::handleWhiteBalancePick(const QString &filePath,
                                        int temperatureKelvin,
                                        int tint,
                                        const QString &error)
{
    if (filePath != m_currentFilePath) {
        return;
    }

    if (!error.isEmpty() || temperatureKelvin <= 0) {
        QMessageBox::warning(
            this,
            tr("Could not set white balance"),
            tr("Nexus could not use that area as a neutral white-balance sample.\n\n%1")
                .arg(error));
        return;
    }

    DevelopState state = m_requestedState;
    state.temperatureKelvin = temperatureKelvin;
    state.tint = tint;

    setDevelopControlsFromState(state);
    requestFullRender(state, tr("White Balance Pick"));
}

void MainWindow::handleHistoryEntryActivated(int index)
{
    const std::optional<DevelopState> state =
        m_historyModel.selectEntry(m_currentFilePath, index);
    if (!state) {
        return;
    }

    m_requestedState = *state;
    m_fullQualityState.reset();
    setDevelopControlsFromState(*state);
    refreshHistoryPanel();

    if (index == 0 && !m_originalImage.isNull()) {
        m_canvas->replaceImage(m_originalImage);
        m_navigator->setImage(m_originalImage);
        m_histogram->setImage(m_originalImage);
        m_fullImageSize = m_originalImage.size();
        m_fullQualityState = *state;
        return;
    }

    requestFullRender(*state);
}

void MainWindow::setDevelopControlsFromState(const DevelopState &state)
{
    m_settingDevelopControls = true;

    if (m_temperatureSlider) {
        m_temperatureSlider->setValue(
            temperatureSliderPositionForKelvin(m_temperatureSlider,
                                               state.temperatureKelvin));
    }
    if (m_tintSlider) {
        m_tintSlider->setValue(state.tint);
    }
    if (m_exposureSlider) {
        m_exposureSlider->setValue(state.exposureHundredths);
    }
    if (m_highlightsSlider) {
        m_highlightsSlider->setValue(state.highlights);
    }
    if (m_shadowsSlider) {
        m_shadowsSlider->setValue(state.shadows);
    }
    if (m_whitesSlider) {
        m_whitesSlider->setValue(state.whites);
    }
    if (m_contrastSlider) {
        m_contrastSlider->setValue(state.contrast);
    }
    if (m_textureSlider) {
        m_textureSlider->setValue(state.texture);
    }
    if (m_dehazeSlider) {
        m_dehazeSlider->setValue(state.dehaze);
    }
    if (m_saturationSlider) {
        m_saturationSlider->setValue(state.saturation);
    }
    if (m_vibranceSlider) {
        m_vibranceSlider->setValue(state.vibrance);
    }

    m_settingDevelopControls = false;
}

void MainWindow::refreshHistoryPanel()
{
    if (!m_historyPanel) {
        return;
    }

    m_historyPanel->setEntries(m_historyModel.labels(m_currentFilePath),
                               m_historyModel.currentIndex(m_currentFilePath));
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
