#include "app/MainWindow.h"

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

    auto *menuBar = window.menuBar();
    auto *fileMenu = menuBar->addMenu(QObject::tr("&File"));
    menuBar->addMenu(QObject::tr("&Edit"));
    menuBar->addMenu(QObject::tr("&Photo"));
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
    addPlaceholderAction(fileMenu,
                         QObject::tr("Export…"),
                         QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    addPlaceholderAction(fileMenu, QObject::tr("Export with Previous Settings"));

    fileMenu->addSeparator();
    addPlaceholderAction(fileMenu, QObject::tr("File Info…"));
    addPlaceholderAction(fileMenu, QObject::tr("Reveal in File Explorer"));

    fileMenu->addSeparator();
    QAction *exitAction = fileMenu->addAction(QObject::tr("Exit"));
    exitAction->setShortcut(QKeySequence(QStringLiteral("Alt+F4")));
    QObject::connect(exitAction, &QAction::triggered,
                     &window, &QWidget::close);

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

    window.show();

    return app.exec();
}
