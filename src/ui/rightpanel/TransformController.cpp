#include "ui/rightpanel/TransformController.h"

#include "app/MainWindow.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/rightpanel/CropToolController.h"
#include "ui/rightpanel/TransformPanel.h"

#include <QComboBox>
#include <QGraphicsScene>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

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

    connect(m_panel, &TransformPanel::previewRequested,
            this, &TransformController::previewFromPanel);
    connect(m_panel, &TransformPanel::commitRequested,
            this, &TransformController::commitFromPanel);

    const auto scheduleGeometryRefresh = [this](bool forceFit) {
        QTimer::singleShot(0, this, [this, forceFit]() {
            if (m_cropController && m_window && m_window->m_canvas
                && m_window->m_canvas->hasImage()) {
                m_cropController->refreshGeometryFromWindowState(forceFit);
            }
        });
    };

    if (auto *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        connect(cropSelector, &QToolButton::toggled,
                this,
                [panel = QPointer<TransformPanel>(m_panel), scheduleGeometryRefresh]
                (bool cropEditing) {
                    if (panel) panel->setVisible(!cropEditing);
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
                        }
                    });
                });
    }

    syncFromWindowState();
    if (m_window->m_canvas && m_window->m_canvas->hasImage()) {
        m_cropController->refreshGeometryFromWindowState(false);
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

    if (auto *cropSelector = m_window->findChild<QToolButton *>(
            QStringLiteral("cropToolSelector"))) {
        m_panel->setVisible(!cropSelector->isChecked());
    }

    if (hasPhoto && !m_panel->isEditing()) {
        m_panel->setFromState(m_window->m_requestedState);
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
    m_window->m_requestedState = state;

    if (m_window->m_historyModel.commit(m_window->m_currentFilePath,
                                        historyLabel,
                                        state)) {
        m_window->refreshHistoryPanel();
    }

    m_cropController->previewGeometryState(state, false);
}
