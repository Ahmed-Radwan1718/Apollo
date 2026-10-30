#include "app/PhotoFlipController.h"

#include "app/MainWindow.h"
#include "imaging/CropTransform.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/rightpanel/CropToolController.h"

#include <QAction>
#include <QGraphicsScene>
#include <QMenu>
#include <QRectF>
#include <QToolButton>

PhotoFlipController::PhotoFlipController(MainWindow *window,
                                         CropToolController *cropController,
                                         QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_cropController(cropController)
{
    if (!m_window || !m_window->m_canvas) {
        return;
    }

    if (QGraphicsScene *scene = m_window->m_canvas->scene()) {
        connect(scene, &QGraphicsScene::changed,
                this,
                [this]() {
                    updateActionAvailability();
                });
    }

    if (QToolButton *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        connect(cropSelector, &QToolButton::toggled,
                this,
                [this](bool) {
                    updateActionAvailability();
                });
    }
}

void PhotoFlipController::addActions(QMenu *photoMenu)
{
    if (!photoMenu || m_flipHorizontalAction || m_flipVerticalAction) {
        return;
    }

    m_flipHorizontalAction = photoMenu->addAction(tr("Flip Horizontal"));
    m_flipVerticalAction = photoMenu->addAction(tr("Flip Vertical"));

    connect(m_flipHorizontalAction, &QAction::triggered,
            this, [this]() { applyFlip(true); });
    connect(m_flipVerticalAction, &QAction::triggered,
            this, [this]() { applyFlip(false); });

    updateActionAvailability();
}

void PhotoFlipController::applyFlip(bool horizontal)
{
    if (!m_window || !m_cropController || !m_window->m_canvas
        || !m_window->m_canvas->hasImage()
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    if (QToolButton *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"));
        cropSelector && cropSelector->isChecked()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    const QRectF crop = imaging::cropRectFromState(state);
    const QRectF mirroredCrop = horizontal
        ? QRectF(1.0 - crop.x() - crop.width(),
                 crop.y(),
                 crop.width(),
                 crop.height())
        : QRectF(crop.x(),
                 1.0 - crop.y() - crop.height(),
                 crop.width(),
                 crop.height());
    imaging::setCropRectOnState(state, mirroredCrop);

    if (horizontal) {
        state.cropFlipHorizontal = !state.cropFlipHorizontal;
    } else {
        state.cropFlipVertical = !state.cropFlipVertical;
    }

    m_window->m_requestedState = state;
    const QString historyLabel = horizontal
        ? tr("Flip Horizontal")
        : tr("Flip Vertical");
    if (m_window->m_historyModel.commit(m_window->m_currentFilePath,
                                        historyLabel,
                                        state)) {
        m_window->refreshHistoryPanel();
    }

    m_cropController->refreshGeometryFromWindowState(true);
}

void PhotoFlipController::updateActionAvailability()
{
    const bool hasPhoto = m_window
        && m_window->m_canvas
        && m_window->m_canvas->hasImage()
        && !m_window->m_currentFilePath.isEmpty();
    const QToolButton *cropSelector = m_window
        ? m_window->findChild<QToolButton *>(QStringLiteral("cropToolSelector"))
        : nullptr;
    const bool enabled = hasPhoto && (!cropSelector || !cropSelector->isChecked());

    if (m_flipHorizontalAction) {
        m_flipHorizontalAction->setEnabled(enabled);
    }
    if (m_flipVerticalAction) {
        m_flipVerticalAction->setEnabled(enabled);
    }
}
