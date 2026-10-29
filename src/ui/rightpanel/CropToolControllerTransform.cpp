#include "ui/rightpanel/CropToolController.h"

#include "app/MainWindow.h"
#include "imaging/CropTransform.h"
#include "imaging/GeometryTransform.h"
#include "ui/canvas/CropOverlayWidget.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/leftpanel/NavigatorPanel.h"

#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QSize>
#include <QToolButton>
#include <QTransform>

#include <algorithm>

namespace {

QGraphicsItem *photoItem(PhotoCanvas *canvas)
{
    if (!canvas || !canvas->scene()) {
        return nullptr;
    }
    const QList<QGraphicsItem *> items = canvas->scene()->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : items) {
        if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
            return item;
        }
    }
    return nullptr;
}

} // namespace

void CropToolController::previewGeometryState(const DevelopState &state, bool forceFit)
{
    if (!m_window || !m_window->m_canvas || !m_window->m_canvas->scene()) {
        return;
    }

    QGraphicsItem *imageItem = photoItem(m_window->m_canvas);
    if (!imageItem) {
        return;
    }

    m_window->m_canvas->setProperty("apolloCropQuarterTurns",
                                    imaging::normalizedQuarterTurns(state.cropQuarterTurns));

    if (!imaging::hasManualTransform(state)) {
        imageItem->setTransform(QTransform());
        if (m_overlay) {
            m_overlay->setReferenceSceneRect({});
        }
        applyGeometryToCanvas(state, m_editing, forceFit);
        return;
    }

    const imaging::GeometryMapping mapping =
        imaging::geometryMappingForState(sourceImageSize(), state);
    if (!mapping.valid || mapping.canvasRect.isEmpty()) {
        imageItem->setTransform(QTransform());
        if (m_overlay) {
            m_overlay->setReferenceSceneRect({});
        }
        applyGeometryToCanvas(state, m_editing, forceFit);
        return;
    }

    const bool hadGeometry = m_haveAppliedState
        && imaging::hasCropGeometry(m_appliedState);
    m_applyingView = true;

    imageItem->setTransformOriginPoint(QPointF(0.0, 0.0));
    imageItem->setRotation(0.0);
    imageItem->setTransform(mapping.transform);

    const QRectF canvasRect = mapping.canvasRect;
    const QRectF normalized = imaging::effectiveCropRectFromState(sourceImageSize(), state);
    const QRectF cropRect(
        canvasRect.left() + normalized.left() * canvasRect.width(),
        canvasRect.top() + normalized.top() * canvasRect.height(),
        normalized.width() * canvasRect.width(),
        normalized.height() * canvasRect.height());

    const bool geometry = imaging::hasCropGeometry(state);
    const QRectF displayRect = !m_editing && geometry ? cropRect : canvasRect;
    m_window->m_canvas->scene()->setSceneRect(displayRect);

    if (m_overlay) {
        m_overlay->setReferenceSceneRect(canvasRect);
        m_overlay->setAllowedRect(m_editing
            ? imaging::safeCropRectForGeometry(sourceImageSize(), state)
            : QRectF(0.0, 0.0, 1.0, 1.0));
        m_overlay->setNormalizedRect(normalized);
        m_overlay->setAspectRatio(selectedAspectRatio(state));
        m_overlay->setEditing(m_editing);
        m_overlay->setGeometry(m_window->m_canvas->viewport()->rect());
    }

    if (m_editing) {
        fitSceneRect(canvasRect, 24);
    } else if (geometry && (forceFit || m_window->m_canvas->isFitZoom())) {
        fitSceneRect(displayRect, 24);
    } else if (!geometry && hadGeometry && (forceFit || m_window->m_canvas->isFitZoom())) {
        m_window->m_canvas->fitToView();
    }

    if (m_overlay) {
        const bool needsMask = normalized.x() > 0.00001
            || normalized.y() > 0.00001
            || normalized.width() < 0.99999
            || normalized.height() < 0.99999;
        m_overlay->setVisible(m_editing || needsMask);
        if (m_overlay->isVisible()) {
            m_overlay->raise();
            m_overlay->update();
        }
    }

    if (!m_editing && m_window->m_navigator) {
        QSize targetSize = displayRect.size().toSize();
        if (targetSize.isValid() && !targetSize.isEmpty()) {
            targetSize.scale(QSize(640, 640), Qt::KeepAspectRatio);
            targetSize.setWidth(std::max(1, targetSize.width()));
            targetSize.setHeight(std::max(1, targetSize.height()));
            QImage preview(targetSize, QImage::Format_RGB32);
            preview.fill(QColor(24, 24, 24));
            QPainter painter(&preview);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
            m_window->m_canvas->scene()->render(
                &painter,
                QRectF(QPointF(0.0, 0.0), QSizeF(targetSize)),
                displayRect,
                Qt::KeepAspectRatio);
            painter.end();
            m_window->m_navigator->setImage(preview);
        }
    }

    m_appliedState = state;
    m_haveAppliedState = true;
    m_applyingView = false;
}

void CropToolController::refreshGeometryFromWindowState(bool forceFit)
{
    if (!m_window || !m_window->m_canvas) {
        return;
    }

    const bool hasPhoto = m_window->m_canvas->hasImage()
        && !m_window->m_currentFilePath.isEmpty();
    const bool photoChangedWhileEditing = m_editing
        && m_window->m_currentFilePath != m_workingFilePath;
    const bool selectorNeedsSync = m_selectorButton
        && m_selectorButton->isEnabled() != hasPhoto;

    // Keep Crop's original lifecycle sync only when it is actually needed.
    // Calling it on every projective refresh would temporarily reapply item
    // rotation and could oscillate against the homography for rotated photos.
    if (photoChangedWhileEditing || selectorNeedsSync || !hasPhoto) {
        syncFromWindowState();
    }

    if (!hasPhoto) {
        return;
    }

    if (m_editing) {
        DevelopState state = m_workingState;
        state.transformVertical = m_window->m_requestedState.transformVertical;
        state.transformHorizontal = m_window->m_requestedState.transformHorizontal;
        state.transformRotateHundredths = m_window->m_requestedState.transformRotateHundredths;
        state.transformAspect = m_window->m_requestedState.transformAspect;
        state.transformScale = m_window->m_requestedState.transformScale;
        state.transformXOffset = m_window->m_requestedState.transformXOffset;
        state.transformYOffset = m_window->m_requestedState.transformYOffset;
        state.transformConstrainCrop = m_window->m_requestedState.transformConstrainCrop;
        previewGeometryState(state, forceFit);
        return;
    }

    previewGeometryState(m_window->m_requestedState, forceFit);
}
