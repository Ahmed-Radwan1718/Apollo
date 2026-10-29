#include "ui/rightpanel/TransformController.h"

#include "app/MainWindow.h"
#include "imaging/UprightAnalysis.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/canvas/UprightGuideOverlay.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/rightpanel/CropToolController.h"
#include "ui/rightpanel/TransformPanel.h"

#include <QComboBox>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QPoint>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <optional>

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

QString uprightHistoryLabel(imaging::UprightMode mode)
{
    switch (mode) {
    case imaging::UprightMode::Auto:
        return QObject::tr("Upright Auto");
    case imaging::UprightMode::Guided:
        return QObject::tr("Upright Guided");
    case imaging::UprightMode::Level:
        return QObject::tr("Upright Level");
    case imaging::UprightMode::Vertical:
        return QObject::tr("Upright Vertical");
    case imaging::UprightMode::Full:
        return QObject::tr("Upright Full");
    case imaging::UprightMode::Off:
    default:
        return QObject::tr("Upright Off");
    }
}

} // namespace

TransformController::TransformController(MainWindow *window,
                                         CropToolController *cropController,
                                         QObject *parent)
    : QObject(parent)
    , m_window(window)
    , m_cropController(cropController)
{
    if (!m_window || !m_cropController) {
        return;
    }

    QWidget *container = m_window->findChild<QWidget *>(QStringLiteral("adjustmentsContainer"));
    auto *layout = container ? qobject_cast<QVBoxLayout *>(container->layout()) : nullptr;
    if (!container || !layout) {
        return;
    }

    m_panel = new TransformPanel(container);
    const int insertionIndex = std::max(0, layout->count() - 1);
    layout->insertWidget(insertionIndex, m_panel);

    if (m_window->m_canvas && m_window->m_canvas->viewport()) {
        m_guideOverlay = new UprightGuideOverlay(m_window->m_canvas->viewport());
        m_guideOverlay->setMappingCallbacks(
            [this](const QPointF &viewportPoint) -> std::optional<QPointF> {
                if (!m_window || !m_window->m_canvas || m_window->m_originalImage.isNull()) {
                    return std::nullopt;
                }
                QGraphicsItem *item = photoItem(m_window->m_canvas);
                if (!item) {
                    return std::nullopt;
                }

                bool invertible = false;
                const QTransform inverse = item->sceneTransform().inverted(&invertible);
                if (!invertible) {
                    return std::nullopt;
                }

                const QPointF scenePoint = m_window->m_canvas->mapToScene(
                    QPoint(qRound(viewportPoint.x()), qRound(viewportPoint.y())));
                const QPointF sourcePoint = inverse.map(scenePoint);
                const QSize sourceSize = m_window->m_originalImage.size();
                if (sourceSize.width() <= 0 || sourceSize.height() <= 0
                    || sourcePoint.x() < 0.0 || sourcePoint.y() < 0.0
                    || sourcePoint.x() > sourceSize.width()
                    || sourcePoint.y() > sourceSize.height()) {
                    return std::nullopt;
                }

                return QPointF(
                    std::clamp(sourcePoint.x() / sourceSize.width(), 0.0, 1.0),
                    std::clamp(sourcePoint.y() / sourceSize.height(), 0.0, 1.0));
            },
            [this](const QPointF &sourceNormalizedPoint) -> std::optional<QPointF> {
                if (!m_window || !m_window->m_canvas || m_window->m_originalImage.isNull()) {
                    return std::nullopt;
                }
                QGraphicsItem *item = photoItem(m_window->m_canvas);
                if (!item) {
                    return std::nullopt;
                }

                const QSize sourceSize = m_window->m_originalImage.size();
                const QPointF sourcePoint(
                    sourceNormalizedPoint.x() * sourceSize.width(),
                    sourceNormalizedPoint.y() * sourceSize.height());
                const QPointF scenePoint = item->sceneTransform().map(sourcePoint);
                return QPointF(m_window->m_canvas->mapFromScene(scenePoint));
            });
        m_guideOverlay->setGuidesChangedCallback(
            [this](const std::vector<imaging::UprightGuideConstraint> &guides) {
                handleGuidesChanged(guides);
            });
    }

    connect(m_panel, &TransformPanel::previewRequested,
            this, &TransformController::previewFromPanel);
    connect(m_panel, &TransformPanel::commitRequested,
            this, &TransformController::commitFromPanel);
    connect(m_panel, &TransformPanel::uprightModeRequested,
            this, &TransformController::applyUprightMode);

    const auto scheduleGeometryRefresh = [this](bool forceFit) {
        QTimer::singleShot(0, this, [this, forceFit]() {
            if (m_cropController && m_window && m_window->m_canvas
                && m_window->m_canvas->hasImage()) {
                m_cropController->refreshGeometryFromWindowState(forceFit);
                raiseGuideOverlay();
            }
        });
    };

    if (auto *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        connect(cropSelector, &QToolButton::toggled,
                this,
                [this, panel = QPointer<TransformPanel>(m_panel), scheduleGeometryRefresh]
                (bool cropEditing) {
                    if (panel) panel->setVisible(!cropEditing);
                    if (m_guideOverlay) {
                        const bool guided = m_window
                            && m_window->m_requestedState.transformUprightMode
                                == static_cast<int>(imaging::UprightMode::Guided);
                        m_guideOverlay->setActive(!cropEditing && guided);
                    }
                    scheduleGeometryRefresh(true);
                });
    }

    if (QWidget *cropPanel = m_window->findChild<QWidget *>(
            QStringLiteral("cropToolPanel"))) {
        for (QSlider *slider : cropPanel->findChildren<QSlider *>()) {
            connect(slider, &QSlider::valueChanged,
                    this, [scheduleGeometryRefresh](int) {
                        scheduleGeometryRefresh(true);
                    });
        }
        for (QComboBox *combo : cropPanel->findChildren<QComboBox *>()) {
            connect(combo, qOverload<int>(&QComboBox::currentIndexChanged),
                    this, [scheduleGeometryRefresh](int) {
                        scheduleGeometryRefresh(true);
                    });
        }
        for (QToolButton *button : cropPanel->findChildren<QToolButton *>()) {
            connect(button, &QToolButton::clicked,
                    this, [scheduleGeometryRefresh](bool) {
                        scheduleGeometryRefresh(true);
                    });
        }
    }

    if (m_window->m_historyPanel) {
        connect(m_window->m_historyPanel, &HistoryPanel::entryActivated,
                this,
                [this](int) {
                    QTimer::singleShot(0, this, [this]() {
                        syncFromWindowState();
                        if (m_cropController) {
                            m_cropController->refreshGeometryFromWindowState(true);
                            raiseGuideOverlay();
                        }
                    });
                });
    }

    if (m_window->m_canvas) {
        connect(m_window->m_canvas, &PhotoCanvas::zoomChanged,
                this,
                [scheduleGeometryRefresh](qreal) {
                    scheduleGeometryRefresh(false);
                });
    }

    if (m_window->m_canvas && m_window->m_canvas->scene()) {
        QGraphicsScene *scene = m_window->m_canvas->scene();

        // Crop originally owned scene-change synchronization. Transform now routes
        // those changes back through Crop's shared geometry bridge so only one
        // controller writes the final QGraphicsItem transform.
        QObject::disconnect(scene, nullptr, m_cropController, nullptr);

        connect(scene, &QGraphicsScene::changed,
                this,
                [this](const QList<QRectF> &) {
                    if (m_syncScheduled) {
                        return;
                    }
                    m_syncScheduled = true;
                    QTimer::singleShot(0, this, [this]() {
                        m_syncScheduled = false;
                        syncFromWindowState();
                        if (m_cropController && m_window && m_window->m_canvas
                            && m_window->m_canvas->hasImage()) {
                            m_cropController->refreshGeometryFromWindowState(false);
                            raiseGuideOverlay();
                        }
                    });
                });
    }

    syncFromWindowState();
    if (m_window->m_canvas && m_window->m_canvas->hasImage()) {
        m_cropController->refreshGeometryFromWindowState(false);
        raiseGuideOverlay();
    }
}

void TransformController::syncFromWindowState()
{
    if (!m_window || !m_panel) {
        return;
    }

    const bool hasPhoto = m_window->m_canvas
        && m_window->m_canvas->hasImage()
        && !m_window->m_currentFilePath.isEmpty();
    m_panel->setEnabled(hasPhoto);

    bool cropEditing = false;
    if (auto *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        cropEditing = cropSelector->isChecked();
        m_panel->setVisible(!cropEditing);
    }

    if (m_guideFilePath != m_window->m_currentFilePath) {
        m_guideFilePath = m_window->m_currentFilePath;
        if (m_guideOverlay) {
            m_guideOverlay->clearGuides();
        }
    }

    if (hasPhoto && !m_panel->isEditing()) {
        m_panel->setFromState(m_window->m_requestedState);
    }

    if (m_guideOverlay) {
        const bool guided = hasPhoto
            && !cropEditing
            && m_window->m_requestedState.transformUprightMode
                == static_cast<int>(imaging::UprightMode::Guided);
        m_guideOverlay->setActive(guided);
        if (guided) {
            raiseGuideOverlay();
        }
    }
}

void TransformController::previewFromPanel()
{
    if (!m_window || !m_panel || !m_cropController || !m_window->m_canvas
        || !m_window->m_canvas->hasImage() || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    m_panel->applyToState(state);
    m_window->m_requestedState = state;
    m_cropController->previewGeometryState(state, false);
    updateGuideOverlayForState(state);
    raiseGuideOverlay();
}

void TransformController::commitFromPanel(const QString &historyLabel)
{
    if (!m_window || !m_panel || !m_cropController || historyLabel.isEmpty()
        || !m_window->m_canvas || !m_window->m_canvas->hasImage()
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    m_panel->applyToState(state);
    commitState(state, historyLabel);
}

void TransformController::applyUprightMode(int mode)
{
    if (!m_window || !m_panel || !m_cropController || !m_window->m_canvas
        || !m_window->m_canvas->hasImage() || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    const imaging::UprightMode uprightMode = static_cast<imaging::UprightMode>(
        std::clamp(mode, 0, 5));
    const bool alreadyGuided =
        m_window->m_requestedState.transformUprightMode
            == static_cast<int>(imaging::UprightMode::Guided)
        && uprightMode == imaging::UprightMode::Guided;
    if (alreadyGuided) {
        updateGuideOverlayForState(m_window->m_requestedState);
        raiseGuideOverlay();
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.transformUprightMode = static_cast<int>(uprightMode);

    if (m_guideOverlay) {
        m_guideOverlay->clearGuides();
    }
    m_guideFilePath = m_window->m_currentFilePath;

    if (uprightMode != imaging::UprightMode::Off) {
        // Upright starts from a neutral manual Transform state, matching the
        // established Upright workflow; the sliders remain available afterwards
        // for refinement. Constrain Crop is intentionally preserved.
        state.transformVertical = 0;
        state.transformHorizontal = 0;
        state.transformRotateHundredths = 0;
        state.transformAspect = 0;
        state.transformScale = 100;
        state.transformXOffset = 0;
        state.transformYOffset = 0;
    }

    if (uprightMode == imaging::UprightMode::Off
        || uprightMode == imaging::UprightMode::Guided) {
        state.transformVertical = 0;
        state.transformHorizontal = 0;
        state.transformRotateHundredths = 0;
    } else {
        const imaging::UprightCorrection correction = imaging::estimateAutomaticUpright(
            m_window->m_originalImage,
            state,
            uprightMode);
        state.transformVertical = correction.valid ? correction.vertical : 0;
        state.transformHorizontal = correction.valid ? correction.horizontal : 0;
        state.transformRotateHundredths =
            correction.valid ? correction.rotateHundredths : 0;
    }

    commitState(state, uprightHistoryLabel(uprightMode));
}

void TransformController::handleGuidesChanged(
    const std::vector<imaging::UprightGuideConstraint> &guides)
{
    if (!m_window || !m_panel || !m_cropController || guides.size() < 2
        || m_window->m_originalImage.isNull()
        || m_window->m_requestedState.transformUprightMode
            != static_cast<int>(imaging::UprightMode::Guided)) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    const imaging::UprightCorrection correction = imaging::estimateGuidedUpright(
        QSizeF(m_window->m_originalImage.size()),
        state,
        guides);
    if (!correction.valid) {
        return;
    }

    state.transformVertical = correction.vertical;
    state.transformHorizontal = correction.horizontal;
    state.transformRotateHundredths = correction.rotateHundredths;
    commitState(state, uprightHistoryLabel(imaging::UprightMode::Guided));
}

void TransformController::commitState(const DevelopState &state,
                                      const QString &historyLabel)
{
    if (!m_window || !m_panel || !m_cropController || historyLabel.isEmpty()) {
        return;
    }

    m_window->m_requestedState = state;
    m_panel->setFromState(state);
    if (m_window->m_historyModel.commit(m_window->m_currentFilePath,
                                        historyLabel,
                                        state)) {
        m_window->refreshHistoryPanel();
    }

    m_cropController->previewGeometryState(state, false);
    updateGuideOverlayForState(state);
    raiseGuideOverlay();
}

void TransformController::updateGuideOverlayForState(const DevelopState &state)
{
    if (!m_guideOverlay || !m_window || !m_window->m_canvas) {
        return;
    }

    bool cropEditing = false;
    if (auto *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        cropEditing = cropSelector->isChecked();
    }

    const bool active = !cropEditing
        && m_window->m_canvas->hasImage()
        && state.transformUprightMode
            == static_cast<int>(imaging::UprightMode::Guided);
    m_guideOverlay->setActive(active);
}

void TransformController::raiseGuideOverlay()
{
    if (!m_guideOverlay || !m_guideOverlay->isActive() || !m_window
        || !m_window->m_canvas || !m_window->m_canvas->viewport()) {
        return;
    }

    m_guideOverlay->setGeometry(m_window->m_canvas->viewport()->rect());
    m_guideOverlay->raise();
    m_guideOverlay->update();
}
