#include "ui/library/LibraryWorkspaceController.h"

#include "app/ApplicationStateStore.h"
#include "app/MainWindow.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/filmstrip/FilmstripWidget.h"
#include "ui/histogram/HistogramWidget.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/leftpanel/NavigatorPanel.h"
#include "ui/library/LibraryGridWidget.h"
#include "ui/library/LibraryImportService.h"
#include "ui/library/LibrarySidebar.h"
#include "ui/rightpanel/BasicPanel.h"
#include "ui/rightpanel/PhotoMetadataWidget.h"
#include "ui/workspace/WorkspaceModeBar.h"

#include <QDir>
#include <QFileInfo>
#include <QMessageBox>
#include <QMetaObject>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

LibraryWorkspaceController::LibraryWorkspaceController(
    MainWindow *window,
    WorkspaceModeBar *modeBar,
    QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_modeBar(modeBar)
{
    if (!m_window || !m_modeBar) {
        return;
    }

    QWidget *leftPanel = m_window->findChild<QWidget *>(QStringLiteral("leftPanel"));
    QWidget *centerPanel = m_window->findChild<QWidget *>(QStringLiteral("centerPanel"));

    m_navigator = m_window->findChild<NavigatorPanel *>();
    m_history = m_window->findChild<HistoryPanel *>();
    m_canvas = m_window->findChild<PhotoCanvas *>();
    m_histogram = m_window->findChild<HistogramWidget *>();
    m_photoMetadata = m_window->findChild<PhotoMetadataWidget *>();

    if (QWidget *filmstripWidget =
            m_window->findChild<QWidget *>(QStringLiteral("filmstripWidget"))) {
        m_filmstrip = static_cast<FilmstripWidget *>(filmstripWidget);
    }

    if (QWidget *basicPanelWidget =
            m_window->findChild<QWidget *>(QStringLiteral("basicPanel"))) {
        m_basicPanel = static_cast<BasicPanel *>(basicPanelWidget);
    }

    if (leftPanel) {
        if (auto *leftLayout = qobject_cast<QVBoxLayout *>(leftPanel->layout())) {
            m_sidebar = new LibrarySidebar(leftPanel);
            leftLayout->addWidget(m_sidebar, 1);
            m_sidebar->hide();
        }
    }

    if (centerPanel) {
        if (auto *centerLayout = qobject_cast<QVBoxLayout *>(centerPanel->layout())) {
            m_grid = new LibraryGridWidget(centerPanel);
            const int canvasIndex = m_canvas ? centerLayout->indexOf(m_canvas) : -1;
            centerLayout->insertWidget(canvasIndex >= 0 ? canvasIndex : 1, m_grid, 1);
            m_grid->hide();
        }
    }

    m_importService = new LibraryImportService(this);
    m_importService->setPhotoReadyCallback(
        [this](const QString &filePath,
               const QImage &thumbnail,
               const imaging::PhotoMetadata &metadata) {
            ApplicationStateStore::addLibraryPhoto(filePath);
            m_libraryMetadata.insert(filePath, metadata);

            if (m_grid) {
                m_grid->addPhoto(filePath, thumbnail);
            }
            if (m_sidebar) {
                m_sidebar->addFolder(QFileInfo(filePath).absolutePath());
            }
            if (m_filmstrip) {
                m_filmstrip->addPhoto(filePath, thumbnail);
            }
        });

    m_importService->setDevelopPhotoReadyCallback(
        [this](const QString &filePath, const QString &error) {
            if (!m_window) {
                return;
            }

            if (!error.isEmpty()) {
                if (m_basicPanel) {
                    m_basicPanel->setEnabled(true);
                }

                QMessageBox::critical(
                    m_window,
                    tr("Could not open photo"),
                    tr("Apollo could not decode:\n%1\n\n%2")
                        .arg(QDir::toNativeSeparators(filePath), error));
                return;
            }

            // ImageLoader has already decoded and cached this image on a worker.
            // Reuse MainWindow's normal open path so history/develop state remains
            // exactly the same, but the expensive decoder no longer blocks the UI.
            QMetaObject::invokeMethod(m_window,
                                      "openImage",
                                      Qt::DirectConnection,
                                      Q_ARG(QString, filePath));

            if (m_basicPanel) {
                m_basicPanel->setEnabled(true);
            }
        });

    if (m_filmstrip) {
        m_filmstrip->setPhotoOpenedCallback(
            [this](const QString &filePath, const QImage &image) {
                ApplicationStateStore::addLibraryPhoto(filePath);

                if (m_grid) {
                    m_grid->addPhoto(filePath, image);
                }
                if (m_sidebar) {
                    m_sidebar->addFolder(QFileInfo(filePath).absolutePath());
                }
            });

        m_filmstrip->setPhotoRemovedCallback(
            [this](const QString &filePath) {
                const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
                ApplicationStateStore::removeLibraryPhoto(absoluteFilePath);
                m_libraryMetadata.remove(filePath);
                m_libraryMetadata.remove(absoluteFilePath);

                if (m_grid) {
                    m_grid->removePhoto(absoluteFilePath);
                }
            });
    }

    if (m_grid) {
        m_grid->setPhotoActivatedCallback(
            [this](const QString &filePath, const QImage &preview) {
                if (!m_window || filePath.isEmpty()) {
                    return;
                }

                // Show the already-available Library preview immediately. Full
                // resolution decode continues in the background below.
                if (!preview.isNull()) {
                    if (m_canvas) {
                        m_canvas->setImage(preview);
                    }
                    if (m_navigator) {
                        m_navigator->setImage(preview);
                    }
                    if (m_histogram) {
                        m_histogram->setImage(preview);
                    }
                    if (m_filmstrip) {
                        m_filmstrip->addPhoto(filePath, preview);
                    }
                }

                if (m_photoMetadata) {
                    const auto metadata = m_libraryMetadata.constFind(filePath);
                    if (metadata != m_libraryMetadata.constEnd()) {
                        m_photoMetadata->setMetadata(*metadata);
                    } else {
                        m_photoMetadata->clear();
                    }
                }

                if (m_basicPanel) {
                    m_basicPanel->setEnabled(false);
                }

                m_window->setWindowTitle(
                    QStringLiteral("%1 — Apollo").arg(QFileInfo(filePath).fileName()));

                if (m_importService) {
                    m_importService->requestDevelopPhoto(filePath);
                }
            });

        m_grid->setFilesDroppedCallback(
            [this](const QStringList &filePaths) {
                if (m_importService) {
                    m_importService->importFiles(filePaths);
                }
            });
    }

    const QStringList savedLibraryPhotos = ApplicationStateStore::loadLibraryPhotos();
    if (m_importService && !savedLibraryPhotos.isEmpty()) {
        m_importService->importFiles(savedLibraryPhotos);
    }

    connect(m_modeBar->libraryButton(), &QPushButton::clicked,
            this, [this]() { setLibraryMode(true); });
    connect(m_modeBar->developButton(), &QPushButton::clicked,
            this, [this]() { setLibraryMode(false); });

    setLibraryMode(false);
}

void LibraryWorkspaceController::setLibraryMode(bool libraryMode)
{
    m_libraryMode = libraryMode;

    if (m_modeBar) {
        m_modeBar->setLibraryMode(libraryMode);
        if (m_modeBar->layout()) {
            m_modeBar->layout()->setContentsMargins(0, 0, 0, 0);
        }
    }

    if (m_window) {
        if (QWidget *leftPanel =
                m_window->findChild<QWidget *>(QStringLiteral("leftPanel"))) {
            leftPanel->show();
        }
        if (QWidget *developToolStrip =
                m_window->findChild<QWidget *>(QStringLiteral("developToolStrip"))) {
            developToolStrip->setVisible(!libraryMode);
        }

        const QStringList editorialPanelNames = {
            QStringLiteral("basicPanel"),
            QStringLiteral("detailPanel"),
            QStringLiteral("calibrationPanel"),
            QStringLiteral("transformPanel"),
            QStringLiteral("effectsPanel")
        };
        for (const QString &panelName : editorialPanelNames) {
            if (QWidget *panel = m_window->findChild<QWidget *>(panelName)) {
                panel->setVisible(!libraryMode);
            }
        }
    }

    if (m_navigator) {
        m_navigator->setVisible(!libraryMode);
    }
    if (m_history) {
        m_history->setVisible(!libraryMode);
    }
    if (m_canvas) {
        m_canvas->setVisible(!libraryMode);
    }
    if (m_filmstrip) {
        m_filmstrip->setVisible(!libraryMode);
    }
    if (m_sidebar) {
        m_sidebar->setVisible(libraryMode);
    }
    if (m_grid) {
        m_grid->setVisible(libraryMode);
    }
}
