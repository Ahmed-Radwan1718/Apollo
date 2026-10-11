#include "app/MainWindow.h"

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
#include <QMessageBox>
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

    createActions();

    connect(m_canvas, &PhotoCanvas::fileDropped,
            this, &MainWindow::openImage);
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

    m_canvas->setImage(result.image);
    m_navigator->setImage(result.image);
    m_histogram->setImage(result.image);
    m_photoMetadata->setMetadata(result.metadata);
    m_basicPanel->setCameraTemperatureKelvin(result.metadata.whiteBalanceTemperatureKelvin);
    setWindowTitle(QStringLiteral("%1 — Nexus").arg(QFileInfo(filePath).fileName()));
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
