#include "app/MainWindow.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
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

    if (QAction *openAction = findWindowAction(QObject::tr("Open Photo"))) {
        fileMenu->addAction(openAction);
    }

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
