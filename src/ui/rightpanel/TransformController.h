#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <vector>

struct DevelopState;
class CropToolController;
class MainWindow;
class TransformPanel;
class UprightGuideOverlay;

namespace imaging {
struct UprightGuideConstraint;
}

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
    void applyUprightMode(int mode);
    void handleGuidesChanged(
        const std::vector<imaging::UprightGuideConstraint> &guides);
    void commitState(const DevelopState &state, const QString &historyLabel);
    void updateGuideOverlayForState(const DevelopState &state);
    void raiseGuideOverlay();

    MainWindow *m_window = nullptr;
    CropToolController *m_cropController = nullptr;
    QPointer<TransformPanel> m_panel;
    QPointer<UprightGuideOverlay> m_guideOverlay;
    QString m_guideFilePath;
    bool m_syncScheduled = false;
};
