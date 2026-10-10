#include "app/MainWindow.h"

#include "imaging/ImageLoader.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/histogram/HistogramWidget.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
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

    m_canvas = new PhotoCanvas(workspace);
    workspaceLayout->addWidget(m_canvas, 1);

    auto *rightPanel = new QWidget(workspace);
    rightPanel->setObjectName(QStringLiteral("rightPanel"));
    rightPanel->setFixedWidth(320);

    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(12, 12, 12, 12);
    rightLayout->setSpacing(8);

    auto *histogramTitle = new QLabel(tr("HISTOGRAM"), rightPanel);
    histogramTitle->setObjectName(QStringLiteral("histogramTitle"));
    rightLayout->addWidget(histogramTitle);

    m_histogram = new HistogramWidget(rightPanel);
    rightLayout->addWidget(m_histogram);
    rightLayout->addStretch(1);

    workspaceLayout->addWidget(rightPanel);
    setCentralWidget(workspace);

    resize(1440, 900);
    setMinimumSize(800, 520);
    setWindowTitle(QStringLiteral("Nexus"));

    setStyleSheet(QStringLiteral(
        "QMainWindow { background: #181818; }"
        "QMenuBar { background: #202020; color: #dddddd; }"
        "QMenuBar::item:selected { background: #343434; }"
        "QMenu { background: #242424; color: #dddddd; border: 1px solid #3a3a3a; }"
        "QMenu::item:selected { background: #3a3a3a; }"
        "QWidget#rightPanel { background: #202020; border-left: 1px solid #303030; }"
        "QLabel#histogramTitle { color: #cfcfcf; font-weight: bold; }"));

    createActions();
    createMenus();

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
    m_histogram->setImage(result.image);
    setWindowTitle(QStringLiteral("%1 — Nexus").arg(QFileInfo(filePath).fileName()));
}

void MainWindow::createActions()
{
    m_openAction = new QAction(tr("&Open Photo…"), this);
    m_openAction->setShortcut(QKeySequence::Open);
    connect(m_openAction, &QAction::triggered,
            this, &MainWindow::chooseAndOpenImage);

    m_fitAction = new QAction(tr("&Fit to View"), this);
    m_fitAction->setShortcut(QKeySequence(Qt::Key_F));
    connect(m_fitAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::fitToView);

    m_actualPixelsAction = new QAction(tr("&100% (Actual Pixels)"), this);
    m_actualPixelsAction->setShortcut(QKeySequence(Qt::Key_1));
    connect(m_actualPixelsAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::actualPixels);

    m_zoomInAction = new QAction(tr("Zoom &In"), this);
    m_zoomInAction->setShortcuts(QKeySequence::ZoomIn);
    connect(m_zoomInAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::zoomIn);

    m_zoomOutAction = new QAction(tr("Zoom &Out"), this);
    m_zoomOutAction->setShortcuts(QKeySequence::ZoomOut);
    connect(m_zoomOutAction, &QAction::triggered,
            m_canvas, &PhotoCanvas::zoomOut);
}

void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(tr("&File"));
    fileMenu->addAction(m_openAction);

    QMenu *viewMenu = menuBar()->addMenu(tr("&View"));
    viewMenu->addAction(m_fitAction);
    viewMenu->addAction(m_actualPixelsAction);
    viewMenu->addSeparator();
    viewMenu->addAction(m_zoomInAction);
    viewMenu->addAction(m_zoomOutAction);
}
