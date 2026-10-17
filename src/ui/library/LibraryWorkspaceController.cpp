#include "ui/library/LibraryWorkspaceController.h"

#include "app/MainWindow.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/filmstrip/FilmstripWidget.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/leftpanel/NavigatorPanel.h"
#include "ui/library/LibraryGridWidget.h"
#include "ui/library/LibrarySidebar.h"
#include "ui/rightpanel/BasicPanel.h"
#include "ui/workspace/WorkspaceModeBar.h"

#include <QFileInfo>
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

    if (m_filmstrip) {
        m_filmstrip->setPhotoOpenedCallback(
            [this](const QString &filePath, const QImage &image) {
                if (m_grid) {
                    m_grid->addPhoto(filePath, image);
                }
                if (m_sidebar) {
                    m_sidebar->addFolder(QFileInfo(filePath).absolutePath());
                }
            });
    }

    if (m_grid) {
        m_grid->setPhotoActivatedCallback(
            [this](const QString &filePath) {
                if (!m_window || filePath.isEmpty()) {
                    return;
                }

                QMetaObject::invokeMethod(m_window,
                                          "openImage",
                                          Qt::QueuedConnection,
                                          Q_ARG(QString, filePath));
            });
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
    if (m_basicPanel) {
        m_basicPanel->setVisible(!libraryMode);
    }
    if (m_sidebar) {
        m_sidebar->setVisible(libraryMode);
    }
    if (m_grid) {
        m_grid->setVisible(libraryMode);
    }
}
