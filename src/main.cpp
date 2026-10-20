#include "app/MainWindow.h"
#include "app/PhotoExportController.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/library/LibraryWorkspaceController.h"
#include "ui/rightpanel/BasicSliderInteractionController.h"
#include "ui/workspace/WorkspaceModeBar.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);

    QCoreApplication::setApplicationName(QStringLiteral("Nexus"));
    QCoreApplication::setOrganizationName(QStringLiteral("Nexus"));
    QCoreApplication::setApplicationVersion(QStringLiteral("0.1.0"));

    MainWindow window;

    WorkspaceModeBar *workspaceModeBar = nullptr;
    if (QWidget *centerPanel = window.findChild<QWidget *>(QStringLiteral("centerPanel"))) {
        workspaceModeBar = WorkspaceModeBar::install(centerPanel);
    }

    if (workspaceModeBar) {
        new LibraryWorkspaceController(&window, workspaceModeBar, &window);
    }

    if (QWidget *basicPanel = window.findChild<QWidget *>(QStringLiteral("basicPanel"))) {
        new BasicSliderInteractionController(basicPanel, &window);
    }

    new PhotoExportController(&window, &window);

    auto *menuBar = window.menuBar();
    auto *fileMenu = menuBar->addMenu(QObject::tr("&File"));
    auto *editMenu = menuBar->addMenu(QObject::tr("&Edit"));
    auto *photoMenu = menuBar->addMenu(QObject::tr("&Photo"));
    auto *viewMenu = menuBar->addMenu(QObject::tr("&View"));
    menuBar->addMenu(QObject::tr("&Help"));

    const auto findWindowAction = [&window](const QString &text) -> QAction * {
        for (QAction *action : window.actions()) {
            if (action && action->text() == text) {
                return action;
            }
        }
        return nullptr;
    };

    const auto addPlaceholderAction = [](QMenu *menu,
                                         const QString &text,
                                         const QKeySequence &shortcut = {}) -> QAction * {
        QAction *action = menu->addAction(text);
        action->setEnabled(false);
        if (!shortcut.isEmpty()) {
            action->setShortcut(shortcut);
        }
        return action;
    };

    if (QAction *openAction = findWindowAction(QObject::tr("Open Photo"))) {
        openAction->setText(QObject::tr("Open Photo…"));
        fileMenu->addAction(openAction);
    }
    addPlaceholderAction(fileMenu, QObject::tr("Open Folder…"));

    auto *openRecentMenu = fileMenu->addMenu(QObject::tr("Open Recent"));
    addPlaceholderAction(openRecentMenu, QObject::tr("(No Recent Photos)"));

    fileMenu->addSeparator();
    addPlaceholderAction(fileMenu,
                         QObject::tr("Close Photo"),
                         QKeySequence(QStringLiteral("Ctrl+W")));
    addPlaceholderAction(fileMenu, QObject::tr("Close All"));

    fileMenu->addSeparator();
    addPlaceholderAction(fileMenu, QObject::tr("Save Edit State"));
    addPlaceholderAction(fileMenu, QObject::tr("Save Edit State As…"));

    fileMenu->addSeparator();
    if (QAction *exportAction = findWindowAction(QObject::tr("Export…"))) {
        fileMenu->addAction(exportAction);
    }
    addPlaceholderAction(fileMenu, QObject::tr("Export with Previous Settings"));

    fileMenu->addSeparator();
    addPlaceholderAction(fileMenu, QObject::tr("File Info…"));
    addPlaceholderAction(fileMenu, QObject::tr("Reveal in File Explorer"));

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(QObject::tr("Exit"));
    exitAction->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
    QObject::connect(exitAction, &QAction::triggered,
                     &window, &QWidget::close);

    auto *historyPanel = window.findChild<HistoryPanel *>();
    QAction *undoAction = editMenu->addAction(QObject::tr("Undo\tCtrl+Z"));
    QAction *redoAction = editMenu->addAction(QObject::tr("Redo\tCtrl+Shift+Z"));

    if (historyPanel) {
        QObject::connect(undoAction, &QAction::triggered,
                         historyPanel, &HistoryPanel::undo);
        QObject::connect(redoAction, &QAction::triggered,
                         historyPanel, &HistoryPanel::redo);
        QObject::connect(editMenu, &QMenu::aboutToShow,
                         &window,
                         [historyPanel, undoAction, redoAction]() {
                             undoAction->setEnabled(historyPanel->canUndo());
                             redoAction->setEnabled(historyPanel->canRedo());
                         });
        undoAction->setEnabled(historyPanel->canUndo());
        redoAction->setEnabled(historyPanel->canRedo());
    } else {
        undoAction->setEnabled(false);
        redoAction->setEnabled(false);
    }

    editMenu->addSeparator();
    addPlaceholderAction(editMenu,
                         QObject::tr("Copy Settings"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    addPlaceholderAction(editMenu,
                         QObject::tr("Paste Settings"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+V")));
    addPlaceholderAction(editMenu, QObject::tr("Paste Settings To…"));

    editMenu->addSeparator();
    addPlaceholderAction(editMenu, QObject::tr("Reset Adjustments"));
    addPlaceholderAction(editMenu, QObject::tr("Reset Selected Adjustment"));

    editMenu->addSeparator();
    addPlaceholderAction(editMenu, QObject::tr("Preferences…"));

    QAction *revertAction = photoMenu->addAction(QObject::tr("Revert to Original"));
    if (historyPanel) {
        QObject::connect(revertAction, &QAction::triggered,
                         historyPanel,
                         [historyPanel]() {
                             historyPanel->entryActivated(0);
                         });
    } else {
        revertAction->setEnabled(false);
    }

    photoMenu->addSeparator();
    addPlaceholderAction(photoMenu, QObject::tr("Rotate Left"));
    addPlaceholderAction(photoMenu, QObject::tr("Rotate Right"));
    addPlaceholderAction(photoMenu, QObject::tr("Flip Horizontal"));
    addPlaceholderAction(photoMenu, QObject::tr("Flip Vertical"));

    photoMenu->addSeparator();
    addPlaceholderAction(photoMenu, QObject::tr("Crop…"));
    addPlaceholderAction(photoMenu, QObject::tr("Straighten…"));

    photoMenu->addSeparator();
    addPlaceholderAction(photoMenu, QObject::tr("Before / After"));
    addPlaceholderAction(photoMenu, QObject::tr("Show Original"));

    photoMenu->addSeparator();
    addPlaceholderAction(photoMenu, QObject::tr("Copy Photo Path"));
    addPlaceholderAction(photoMenu, QObject::tr("Show in Filmstrip"));

    if (QAction *fitAction = findWindowAction(QObject::tr("Fit to View"))) {
        viewMenu->addAction(fitAction);
    }
    if (QAction *actualPixelsAction = findWindowAction(QObject::tr("100% (Actual Pixels)"))) {
        viewMenu->addAction(actualPixelsAction);
    }
    viewMenu->addSeparator();
    if (QAction *zoomInAction = findWindowAction(QObject::tr("Zoom In"))) {
        viewMenu->addAction(zoomInAction);
    }
    if (QAction *zoomOutAction = findWindowAction(QObject::tr("Zoom Out"))) {
        viewMenu->addAction(zoomOutAction);
    }

    viewMenu->addSeparator();
    addPlaceholderAction(viewMenu, QObject::tr("Navigator"));
    addPlaceholderAction(viewMenu, QObject::tr("History"));
    addPlaceholderAction(viewMenu, QObject::tr("Histogram"));
    addPlaceholderAction(viewMenu, QObject::tr("Filmstrip"));
    addPlaceholderAction(viewMenu, QObject::tr("Basic Panel"));

    viewMenu->addSeparator();
    addPlaceholderAction(viewMenu, QObject::tr("Show Clipping Warnings"));
    addPlaceholderAction(viewMenu, QObject::tr("Show Grid"));

    viewMenu->addSeparator();
    addPlaceholderAction(viewMenu,
                         QObject::tr("Full Screen"),
                         QKeySequence(QStringLiteral("F11")));

    window.show();

    return app.exec();
}
