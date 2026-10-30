#pragma once

#include <QObject>

class QAction;
class CropToolController;
class MainWindow;
class QMenu;

class PhotoFlipController final : public QObject
{
public:
    explicit PhotoFlipController(MainWindow *window,
                                 CropToolController *cropController,
                                 QObject *parent = nullptr);

    void addActions(QMenu *photoMenu);

private:
    void applyFlip(bool horizontal);
    void updateActionAvailability();

    MainWindow *m_window = nullptr;
    CropToolController *m_cropController = nullptr;
    QAction *m_flipHorizontalAction = nullptr;
    QAction *m_flipVerticalAction = nullptr;
};
