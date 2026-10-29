#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

class CropToolController;
class MainWindow;
class TransformPanel;

class TransformController final : public QObject
{
public:
    TransformController(MainWindow *window,
                        CropToolController *cropController,
                        QObject *parent = nullptr);

private:
    void syncFromWindowState();
    void previewFromPanel();
    void commitFromPanel(const QString &historyLabel);

    MainWindow *m_window = nullptr;
    CropToolController *m_cropController = nullptr;
    QPointer<TransformPanel> m_panel;
    bool m_syncScheduled = false;
};
