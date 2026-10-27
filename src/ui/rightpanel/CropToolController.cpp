#include "ui/rightpanel/CropToolController.h"

#include "app/MainWindow.h"
#include "imaging/CropTransform.h"
#include "ui/canvas/CropOverlayWidget.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/histogram/HistogramWidget.h"
#include "ui/leftpanel/HistoryPanel.h"
#include "ui/leftpanel/NavigatorPanel.h"

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineF>
#include <QList>
#include <QMouseEvent>
#include <QPainter>
#include <QImage>
#include <QPaintEvent>
#include <QPainterPath>
#include <QPolygonF>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QSlider>
#include <QTimer>
#include <QToolButton>
#include <QTransform>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

void copyCropGeometry(const DevelopState &from, DevelopState &to)
{
    to.cropX = from.cropX;
    to.cropY = from.cropY;
    to.cropWidth = from.cropWidth;
    to.cropHeight = from.cropHeight;
    to.cropQuarterTurns = from.cropQuarterTurns;
    to.cropStraightenHundredths = from.cropStraightenHundredths;
    to.cropAspectMode = from.cropAspectMode;
}

class CropSelectorButton final : public QToolButton
{
public:
    explicit CropSelectorButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setFixedSize(34, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Crop & Rotate"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(49, 49, 49));
            painter.drawRoundedRect(rect().adjusted(2, 2, -2, -2), 5.0, 5.0);
        }

        const QColor iconColor = !isEnabled()
            ? QColor(86, 86, 86)
            : (underMouse() || isChecked() ? QColor(244, 244, 244)
                                           : QColor(190, 190, 190));
        painter.setPen(QPen(iconColor, 1.55, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        const QPointF center = QRectF(rect()).center() + QPointF(0.0, 4.5);
        painter.translate(center);
        painter.drawLine(QPointF(-6.0, -7.0), QPointF(-6.0, 4.0));
        painter.drawLine(QPointF(-6.0, 4.0), QPointF(5.0, 4.0));
        painter.drawLine(QPointF(-2.0, -4.0), QPointF(6.0, -4.0));
        painter.drawLine(QPointF(6.0, -4.0), QPointF(6.0, 7.0));
    }
};

class RotateButton final : public QToolButton
{
public:
    explicit RotateButton(bool clockwise, QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_clockwise(clockwise)
    {
        setFixedSize(34, 28);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (underMouse()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(45, 45, 45));
            painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 4.0, 4.0);
        }

        const QColor color = underMouse() ? QColor(245, 245, 245) : QColor(194, 194, 194);
        painter.setPen(QPen(color, 1.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);

        painter.translate(QRectF(rect()).center());
        const QRectF arcRect(-6.0, -6.0, 12.0, 12.0);
        if (m_clockwise) {
            painter.drawArc(arcRect, 55 * 16, -255 * 16);
            QPolygonF head;
            head << QPointF(6.3, 2.0) << QPointF(7.0, 6.0) << QPointF(3.2, 4.6);
            painter.setBrush(color);
            painter.drawPolygon(head);
        } else {
            painter.drawArc(arcRect, 125 * 16, 255 * 16);
            QPolygonF head;
            head << QPointF(-6.3, 2.0) << QPointF(-7.0, 6.0) << QPointF(-3.2, 4.6);
            painter.setBrush(color);
            painter.drawPolygon(head);
        }
    }

private:
    bool m_clockwise = true;
};


} // namespace


CropToolController::CropToolController(MainWindow *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    if (!m_window || !m_window->m_canvas) {
        return;
    }

    buildUi();

    m_overlay = new CropOverlayWidget(m_window->m_canvas, m_window->m_canvas->viewport());
    m_overlay->setGeometry(m_window->m_canvas->viewport()->rect());
    m_overlay->setChangeCallback(
        [this](const QRectF &rect, bool) {
            if (!m_editing) {
                return;
            }
            imaging::setCropRectOnState(m_workingState, rect);
            updateOverlay();
        });

    m_window->m_canvas->installEventFilter(this);
    m_window->m_canvas->viewport()->installEventFilter(this);

    if (QGraphicsScene *scene = m_window->m_canvas->scene()) {
        connect(scene, &QGraphicsScene::changed, this,
                [this](const QList<QRectF> &) { scheduleSync(); });
    }

    connect(m_window->m_canvas, &PhotoCanvas::zoomChanged,
            this,
            [this](qreal) {
                if (m_applyingView || !m_window || !m_window->m_canvas) {
                    return;
                }

                const DevelopState &state = m_editing
                    ? m_workingState
                    : m_window->m_requestedState;
                if (m_window->m_canvas->isFitZoom()
                    && (m_editing || imaging::hasCropGeometry(state))) {
                    QTimer::singleShot(0, this, [this]() {
                        if (m_editing) {
                            applyGeometryToCanvas(m_workingState, true, true);
                        } else if (imaging::hasCropGeometry(m_window->m_requestedState)) {
                            applyGeometryToCanvas(m_window->m_requestedState, false, true);
                        }
                    });
                    return;
                }

                updateOverlay();
            });

    if (m_window->m_historyPanel) {
        connect(m_window->m_historyPanel, &HistoryPanel::entryActivated,
                this,
                [this](int) {
                    m_historyNavigationPending = true;
                    if (m_editing) {
                        finishEditing(false);
                    }
                    QTimer::singleShot(0, this, [this]() {
                        m_historyNavigationPending = false;
                        syncFromWindowState();
                    });
                });
    }

    if (m_window->m_fitAction) {
        connect(m_window->m_fitAction, &QAction::triggered,
                this,
                [this]() {
                    QTimer::singleShot(0, this, [this]() {
                        if (m_editing) {
                            applyGeometryToCanvas(m_workingState, true, true);
                        } else if (imaging::hasCropGeometry(m_window->m_requestedState)) {
                            applyGeometryToCanvas(m_window->m_requestedState, false, true);
                        }
                    });
                });
    }

    if (m_window->m_actualPixelsAction) {
        connect(m_window->m_actualPixelsAction, &QAction::triggered,
                this,
                [this]() {
                    QTimer::singleShot(0, this, [this]() {
                        if (!m_window || !m_window->m_canvas) {
                            return;
                        }
                        const DevelopState &state = m_editing
                            ? m_workingState
                            : m_window->m_requestedState;
                        if (imaging::hasCropGeometry(state)) {
                            m_window->m_canvas->centerOn(cropSceneRect(state).center());
                            updateOverlay();
                        }
                    });
                });
    }

    connect(qApp, &QCoreApplication::aboutToQuit,
            this,
            [this]() {
                if (m_editing) {
                    finishEditing(true);
                }
            });

    scheduleSync();
}

void CropToolController::buildUi()
{
    QWidget *rightPanel = m_window->findChild<QWidget *>(QStringLiteral("rightPanel"));
    auto *rightLayout = rightPanel ? qobject_cast<QVBoxLayout *>(rightPanel->layout()) : nullptr;
    if (!rightPanel || !rightLayout || !m_window->m_histogram) {
        return;
    }

    m_toolStrip = new QWidget(rightPanel);
    m_toolStrip->setObjectName(QStringLiteral("developToolStrip"));
    m_toolStrip->setFixedHeight(36);
    auto *stripLayout = new QHBoxLayout(m_toolStrip);
    stripLayout->setContentsMargins(0, 2, 0, 2);
    stripLayout->setSpacing(4);
    stripLayout->addStretch(1);

    m_selectorButton = new CropSelectorButton(m_toolStrip);
    m_selectorButton->setObjectName(QStringLiteral("cropToolSelector"));
    m_selectorButton->setEnabled(false);
    stripLayout->addWidget(m_selectorButton);
    stripLayout->addStretch(1);

    m_toolStrip->setStyleSheet(QStringLiteral(
        "QWidget#developToolStrip {"
        "  background: transparent;"
        "  border-top: 1px solid #292929;"
        "}"));

    m_panel = new QWidget(rightPanel);
    m_panel->setObjectName(QStringLiteral("cropToolPanel"));
    auto *panelLayout = new QVBoxLayout(m_panel);
    panelLayout->setContentsMargins(2, 4, 2, 7);
    panelLayout->setSpacing(7);

    auto *titleRow = new QHBoxLayout();
    titleRow->setContentsMargins(0, 0, 0, 0);
    auto *title = new QLabel(tr("Crop & Rotate"), m_panel);
    title->setObjectName(QStringLiteral("cropPanelTitle"));
    titleRow->addWidget(title);
    titleRow->addStretch(1);
    m_resetButton = new QToolButton(m_panel);
    m_resetButton->setText(tr("Reset"));
    m_resetButton->setCursor(Qt::PointingHandCursor);
    m_resetButton->setFocusPolicy(Qt::NoFocus);
    titleRow->addWidget(m_resetButton);
    panelLayout->addLayout(titleRow);

    auto *aspectRow = new QHBoxLayout();
    aspectRow->setContentsMargins(0, 0, 0, 0);
    auto *aspectLabel = new QLabel(tr("Aspect"), m_panel);
    aspectLabel->setObjectName(QStringLiteral("cropControlLabel"));
    aspectRow->addWidget(aspectLabel);
    aspectRow->addStretch(1);
    m_aspectCombo = new QComboBox(m_panel);
    m_aspectCombo->addItems({tr("Free"), tr("Original"), tr("1 : 1"),
                             tr("3 : 2"), tr("4 : 3"), tr("16 : 9")});
    m_aspectCombo->setFixedWidth(112);
    aspectRow->addWidget(m_aspectCombo);
    panelLayout->addLayout(aspectRow);

    auto *rotateRow = new QHBoxLayout();
    rotateRow->setContentsMargins(0, 0, 0, 0);
    auto *rotateLabel = new QLabel(tr("Rotate 90°"), m_panel);
    rotateLabel->setObjectName(QStringLiteral("cropControlLabel"));
    rotateRow->addWidget(rotateLabel);
    rotateRow->addStretch(1);
    m_rotateLeftButton = new RotateButton(false, m_panel);
    m_rotateLeftButton->setToolTip(tr("Rotate left 90°"));
    m_rotateRightButton = new RotateButton(true, m_panel);
    m_rotateRightButton->setToolTip(tr("Rotate right 90°"));
    rotateRow->addWidget(m_rotateLeftButton);
    rotateRow->addWidget(m_rotateRightButton);
    panelLayout->addLayout(rotateRow);

    auto *straightenHeader = new QHBoxLayout();
    straightenHeader->setContentsMargins(0, 0, 0, 0);
    auto *straightenLabel = new QLabel(tr("Straighten"), m_panel);
    straightenLabel->setObjectName(QStringLiteral("cropControlLabel"));
    straightenHeader->addWidget(straightenLabel);
    straightenHeader->addStretch(1);
    m_straightenValue = new QLabel(QStringLiteral("0.00°"), m_panel);
    m_straightenValue->setObjectName(QStringLiteral("cropValueLabel"));
    straightenHeader->addWidget(m_straightenValue);
    panelLayout->addLayout(straightenHeader);

    m_straightenSlider = new QSlider(Qt::Horizontal, m_panel);
    m_straightenSlider->setRange(-4500, 4500);
    m_straightenSlider->setSingleStep(10);
    m_straightenSlider->setPageStep(100);
    panelLayout->addWidget(m_straightenSlider);

    m_doneButton = new QToolButton(m_panel);
    m_doneButton->setText(tr("Done"));
    m_doneButton->setCursor(Qt::PointingHandCursor);
    m_doneButton->setFocusPolicy(Qt::NoFocus);
    m_doneButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_doneButton->setFixedHeight(28);
    panelLayout->addWidget(m_doneButton);

    m_panel->setStyleSheet(QStringLiteral(
        "QWidget#cropToolPanel {"
        "  background: #181818;"
        "  border-bottom: 1px solid #292929;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#cropPanelTitle {"
        "  color: #dedede;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#cropControlLabel {"
        "  color: #b9b9b9;"
        "  font-size: 11px;"
        "}"
        "QLabel#cropValueLabel {"
        "  color: #d7d7d7;"
        "  font-size: 11px;"
        "  min-width: 46px;"
        "}"
        "QComboBox {"
        "  color: #dedede;"
        "  background: #222222;"
        "  border: 1px solid #363636;"
        "  border-radius: 4px;"
        "  padding: 3px 7px;"
        "  font-size: 11px;"
        "}"
        "QComboBox:hover { border-color: #505050; }"
        "QComboBox QAbstractItemView {"
        "  color: #dedede;"
        "  background: #222222;"
        "  selection-background-color: #383838;"
        "  border: 1px solid #3a3a3a;"
        "}"
        "QSlider::groove:horizontal {"
        "  height: 3px;"
        "  background: #393939;"
        "  border-radius: 1px;"
        "}"
        "QSlider::handle:horizontal {"
        "  width: 11px;"
        "  margin: -4px 0;"
        "  border-radius: 5px;"
        "  background: #d8d8d8;"
        "}"
        "QToolButton {"
        "  color: #c9c9c9;"
        "  background: transparent;"
        "  border: 0;"
        "  border-radius: 4px;"
        "  padding: 3px 7px;"
        "  font-size: 11px;"
        "}"
        "QToolButton:hover { color: #ffffff; background: #292929; }"
        "QToolButton[text=\"Done\"] {"
        "  color: #eeeeee;"
        "  background: #343434;"
        "  border: 1px solid #454545;"
        "  font-weight: 600;"
        "}"
        "QToolButton[text=\"Done\"]:hover { background: #414141; }"));

    m_panel->hide();

    QWidget *metadataWidget = rightPanel->findChild<QWidget *>(
        QStringLiteral("photoMetadataWidget"), Qt::FindDirectChildrenOnly);
    int insertionIndex = metadataWidget ? rightLayout->indexOf(metadataWidget) : -1;
    if (insertionIndex < 0) {
        insertionIndex = rightLayout->indexOf(m_window->m_histogram);
    }
    if (insertionIndex < 0) {
        insertionIndex = 0;
    }
    rightLayout->insertWidget(insertionIndex + 1, m_toolStrip);
    rightLayout->insertWidget(insertionIndex + 2, m_panel);

    connect(m_selectorButton, &QToolButton::toggled,
            this,
            [this](bool checked) {
                if (checked) {
                    beginEditing();
                } else if (m_editing) {
                    finishEditing(true);
                }
            });
    connect(m_doneButton, &QToolButton::clicked,
            this,
            [this]() {
                if (m_selectorButton) {
                    m_selectorButton->setChecked(false);
                }
            });
    connect(m_resetButton, &QToolButton::clicked,
            this, &CropToolController::resetWorkingGeometry);
    connect(m_rotateLeftButton, &QToolButton::clicked,
            this, [this]() { rotateByQuarterTurns(-1); });
    connect(m_rotateRightButton, &QToolButton::clicked,
            this, [this]() { rotateByQuarterTurns(1); });
    connect(m_aspectCombo, qOverload<int>(&QComboBox::currentIndexChanged),
            this, &CropToolController::setAspectMode);
    connect(m_straightenSlider, &QSlider::valueChanged,
            this, &CropToolController::setStraighten);
}

bool CropToolController::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_window || !m_window->m_canvas || !m_overlay) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == m_window->m_canvas || watched == m_window->m_canvas->viewport()) {
        if (event->type() == QEvent::Resize || event->type() == QEvent::Show) {
            QTimer::singleShot(0, this, [this]() {
                if (!m_window || !m_window->m_canvas) {
                    return;
                }
                if (m_editing) {
                    applyGeometryToCanvas(m_workingState, true, true);
                } else if (imaging::hasCropGeometry(m_window->m_requestedState)
                           && m_window->m_canvas->isFitZoom()) {
                    applyGeometryToCanvas(m_window->m_requestedState, false, true);
                } else {
                    updateOverlay();
                }
            });
        } else if (event->type() == QEvent::MouseMove
                   || event->type() == QEvent::MouseButtonPress
                   || event->type() == QEvent::MouseButtonRelease
                   || event->type() == QEvent::Wheel) {
            QTimer::singleShot(0, this, &CropToolController::updateOverlay);
        }
    }

    return QObject::eventFilter(watched, event);
}

void CropToolController::scheduleSync()
{
    if (m_syncScheduled) {
        return;
    }

    m_syncScheduled = true;
    QTimer::singleShot(0, this, [this]() {
        m_syncScheduled = false;
        syncFromWindowState();
    });
}

void CropToolController::syncFromWindowState()
{
    if (!m_window || !m_window->m_canvas || m_applyingView) {
        return;
    }

    const bool hasPhoto = m_window->m_canvas->hasImage()
        && !m_window->m_currentFilePath.isEmpty();
    if (m_selectorButton) {
        m_selectorButton->setEnabled(hasPhoto);
    }

    if (!hasPhoto) {
        if (m_editing) {
            finishEditing(false);
        }
        if (m_overlay) {
            m_overlay->hide();
        }
        m_haveAppliedState = false;
        return;
    }

    if (m_editing && m_window->m_currentFilePath != m_workingFilePath) {
        commitWorkingStateForPath(m_workingFilePath);
        finishEditing(false);
    }

    if (m_historyNavigationPending) {
        return;
    }

    if (m_editing) {
        DevelopState merged = m_window->m_requestedState;
        copyCropGeometry(m_workingState, merged);
        m_workingState = merged;
        applyGeometryToCanvas(m_workingState, true);
        return;
    }

    applyGeometryToCanvas(m_window->m_requestedState, false);
}

void CropToolController::beginEditing()
{
    if (!m_window || !m_window->m_canvas || !m_window->m_canvas->hasImage()
        || m_window->m_currentFilePath.isEmpty()) {
        if (m_selectorButton) {
            const QSignalBlocker blocker(m_selectorButton);
            m_selectorButton->setChecked(false);
        }
        return;
    }

    m_editing = true;
    m_workingFilePath = m_window->m_currentFilePath;
    m_workingState = m_window->m_requestedState;
    m_editStartState = m_workingState;

    if (m_panel) {
        m_panel->show();
    }
    updateControlsFromState(m_workingState);
    applyGeometryToCanvas(m_workingState, true, true);
}

void CropToolController::finishEditing(bool commitChanges)
{
    if (!m_editing) {
        return;
    }

    if (commitChanges && m_workingFilePath == m_window->m_currentFilePath) {
        if (!m_workingState.geometryEquals(m_editStartState)) {
            m_window->m_requestedState = m_workingState;
            if (m_window->m_historyModel.commit(m_workingFilePath,
                                                tr("Crop & Rotate"),
                                                m_workingState)) {
                m_window->refreshHistoryPanel();
            }
        }
    }

    m_editing = false;
    m_workingFilePath.clear();
    if (m_panel) {
        m_panel->hide();
    }
    if (m_selectorButton && m_selectorButton->isChecked()) {
        const QSignalBlocker blocker(m_selectorButton);
        m_selectorButton->setChecked(false);
    }

    if (m_window && m_window->m_canvas && m_window->m_canvas->hasImage()) {
        applyGeometryToCanvas(m_window->m_requestedState, false, true);
    } else if (m_overlay) {
        m_overlay->hide();
    }
}

void CropToolController::commitWorkingStateForPath(const QString &filePath)
{
    if (!m_window || filePath.isEmpty() || m_workingState.geometryEquals(m_editStartState)) {
        return;
    }

    m_window->m_historyModel.commit(filePath, tr("Crop & Rotate"), m_workingState);
}

void CropToolController::applyGeometryToCanvas(const DevelopState &state,
                                               bool editing,
                                               bool forceFit)
{
    if (!m_window || !m_window->m_canvas || !m_window->m_canvas->scene()) {
        return;
    }

    const QList<QGraphicsItem *> items =
        m_window->m_canvas->scene()->items(Qt::AscendingOrder);
    QGraphicsItem *imageItem = nullptr;
    for (QGraphicsItem *item : items) {
        if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
            imageItem = item;
            break;
        }
    }
    if (!imageItem) {
        return;
    }

    const bool hadGeometry = m_haveAppliedState && imaging::hasCropGeometry(m_appliedState);
    m_applyingView = true;

    imageItem->setTransformOriginPoint(imageItem->boundingRect().center());
    imageItem->setRotation(imaging::cropRotationDegrees(state));
    const QRectF imageSceneRect = imageItem->sceneBoundingRect();
    m_window->m_canvas->scene()->setSceneRect(imageSceneRect);

    if (m_overlay) {
        m_overlay->setAllowedRect(editing
            ? imaging::safeCropRectForStraighten(sourceImageSize(), state)
            : QRectF(0.0, 0.0, 1.0, 1.0));
        m_overlay->setNormalizedRect(imaging::cropRectFromState(state));
        m_overlay->setAspectRatio(selectedAspectRatio(state));
        m_overlay->setEditing(editing);
        m_overlay->setGeometry(m_window->m_canvas->viewport()->rect());
    }

    const bool geometry = imaging::hasCropGeometry(state);
    if (editing) {
        fitSceneRect(imageSceneRect, 24);
    } else if (geometry && (forceFit || m_window->m_canvas->isFitZoom())) {
        fitSceneRect(cropSceneRect(state), 24);
    } else if (!geometry && hadGeometry && (forceFit || m_window->m_canvas->isFitZoom())) {
        m_window->m_canvas->fitToView();
    }

    if (m_overlay) {
        const QRectF normalized = imaging::cropRectFromState(state);
        const bool needsMask = geometry
            && (normalized.x() > 0.00001 || normalized.y() > 0.00001
                || normalized.width() < 0.99999 || normalized.height() < 0.99999);
        m_overlay->setVisible(editing || needsMask);
        if (m_overlay->isVisible()) {
            m_overlay->raise();
            m_overlay->update();
        }
    }

    if (!editing) {
        refreshNavigatorPreview(state);
    }

    m_appliedState = state;
    m_haveAppliedState = true;
    m_applyingView = false;
}

void CropToolController::fitSceneRect(const QRectF &sceneRect, int marginPixels)
{
    if (!m_window || !m_window->m_canvas || sceneRect.isEmpty()) {
        return;
    }

    if (!m_window->m_canvas->isFitZoom()) {
        m_window->m_canvas->fitToView();
    }

    const QSize viewportSize = m_window->m_canvas->viewport()->size();
    const qreal availableWidth = std::max(1, viewportSize.width() - marginPixels * 2);
    const qreal availableHeight = std::max(1, viewportSize.height() - marginPixels * 2);
    const qreal scale = std::min(availableWidth / sceneRect.width(),
                                 availableHeight / sceneRect.height());

    if (!std::isfinite(scale) || scale <= 0.0) {
        return;
    }

    QTransform transform;
    transform.scale(scale, scale);
    m_window->m_canvas->setTransform(transform);
    m_window->m_canvas->centerOn(sceneRect.center());
    updateOverlay();
}

void CropToolController::updateOverlay()
{
    if (!m_overlay || !m_window || !m_window->m_canvas) {
        return;
    }

    m_overlay->setGeometry(m_window->m_canvas->viewport()->rect());
    if (m_overlay->isVisible()) {
        m_overlay->raise();
        m_overlay->update();
    }
}

void CropToolController::refreshNavigatorPreview(const DevelopState &state)
{
    if (!m_window || !m_window->m_navigator || !m_window->m_canvas
        || !m_window->m_canvas->scene()) {
        return;
    }

    QRectF sourceRect;
    if (imaging::hasCropGeometry(state)) {
        sourceRect = cropSceneRect(state);
    } else {
        const QList<QGraphicsItem *> items =
            m_window->m_canvas->scene()->items(Qt::AscendingOrder);
        for (QGraphicsItem *item : items) {
            if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
                sourceRect = item->sceneBoundingRect();
                break;
            }
        }
    }
    if (sourceRect.isEmpty()) {
        return;
    }

    QSize targetSize = sourceRect.size().toSize();
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        return;
    }
    targetSize.scale(QSize(640, 640), Qt::KeepAspectRatio);
    targetSize.setWidth(std::max(1, targetSize.width()));
    targetSize.setHeight(std::max(1, targetSize.height()));

    QImage preview(targetSize, QImage::Format_RGB32);
    preview.fill(QColor(24, 24, 24));
    QPainter painter(&preview);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    m_window->m_canvas->scene()->render(&painter,
                                        QRectF(QPointF(0.0, 0.0), QSizeF(targetSize)),
                                        sourceRect,
                                        Qt::KeepAspectRatio);
    painter.end();
    m_window->m_navigator->setImage(preview);
}

void CropToolController::rotateByQuarterTurns(int delta)
{
    if (!m_editing) {
        return;
    }

    m_workingState.cropQuarterTurns =
        imaging::normalizedQuarterTurns(m_workingState.cropQuarterTurns + delta);
    resetCropForCurrentGeometry();
    updateControlsFromState(m_workingState);
    applyGeometryToCanvas(m_workingState, true, true);
}

void CropToolController::setStraighten(int hundredths)
{
    if (!m_editing) {
        return;
    }

    hundredths = std::clamp(hundredths, -4500, 4500);
    if (m_workingState.cropStraightenHundredths == hundredths) {
        if (m_straightenValue) {
            m_straightenValue->setText(
                QStringLiteral("%1°").arg(hundredths / 100.0, 0, 'f', 2));
        }
        return;
    }

    m_workingState.cropStraightenHundredths = hundredths;
    resetCropForCurrentGeometry();
    if (m_straightenValue) {
        m_straightenValue->setText(
            QStringLiteral("%1°").arg(hundredths / 100.0, 0, 'f', 2));
    }
    applyGeometryToCanvas(m_workingState, true, true);
}

void CropToolController::setAspectMode(int mode)
{
    if (!m_editing) {
        return;
    }

    mode = std::clamp(mode,
                      static_cast<int>(imaging::CropAspectMode::Free),
                      static_cast<int>(imaging::CropAspectMode::Ratio16x9));
    if (m_workingState.cropAspectMode == mode) {
        return;
    }

    m_workingState.cropAspectMode = mode;
    fitCurrentCropToAspect();
    applyGeometryToCanvas(m_workingState, true);
}

void CropToolController::resetWorkingGeometry()
{
    if (!m_editing) {
        return;
    }

    m_workingState.cropX = 0;
    m_workingState.cropY = 0;
    m_workingState.cropWidth = imaging::kCropCoordinateScale;
    m_workingState.cropHeight = imaging::kCropCoordinateScale;
    m_workingState.cropQuarterTurns = 0;
    m_workingState.cropStraightenHundredths = 0;
    m_workingState.cropAspectMode = static_cast<int>(imaging::CropAspectMode::Free);
    updateControlsFromState(m_workingState);
    applyGeometryToCanvas(m_workingState, true, true);
}

void CropToolController::resetCropForCurrentGeometry()
{
    QRectF safeRect = imaging::safeCropRectForStraighten(sourceImageSize(), m_workingState);
    const qreal aspect = selectedAspectRatio(m_workingState);
    if (aspect > 0.0) {
        safeRect = fittedAspectRect(safeRect, aspect, m_workingState);
    }
    imaging::setCropRectOnState(m_workingState, safeRect);
}

void CropToolController::fitCurrentCropToAspect()
{
    const qreal aspect = selectedAspectRatio(m_workingState);
    if (aspect <= 0.0) {
        return;
    }

    const QRectF safe = imaging::safeCropRectForStraighten(sourceImageSize(), m_workingState);
    QRectF current = imaging::cropRectFromState(m_workingState).intersected(safe);
    if (!current.isValid() || current.isEmpty()) {
        current = safe;
    }
    imaging::setCropRectOnState(
        m_workingState,
        fittedAspectRect(current, aspect, m_workingState));
}

qreal CropToolController::selectedAspectRatio(const DevelopState &state) const
{
    const auto mode = static_cast<imaging::CropAspectMode>(state.cropAspectMode);
    switch (mode) {
    case imaging::CropAspectMode::Original: {
        const QSizeF size = sourceImageSize();
        if (!size.isValid() || size.isEmpty()) {
            return 0.0;
        }
        qreal ratio = size.width() / size.height();
        if (imaging::normalizedQuarterTurns(state.cropQuarterTurns) % 2 != 0) {
            ratio = 1.0 / ratio;
        }
        return ratio;
    }
    case imaging::CropAspectMode::Square:
        return 1.0;
    case imaging::CropAspectMode::Ratio3x2:
        return 3.0 / 2.0;
    case imaging::CropAspectMode::Ratio4x3:
        return 4.0 / 3.0;
    case imaging::CropAspectMode::Ratio16x9:
        return 16.0 / 9.0;
    case imaging::CropAspectMode::Free:
    default:
        return 0.0;
    }
}

qreal CropToolController::rotatedBoundingAspect(const DevelopState &state) const
{
    QSizeF size = sourceImageSize();
    if (!size.isValid() || size.isEmpty()) {
        return 1.0;
    }

    qreal width = size.width();
    qreal height = size.height();
    if (imaging::normalizedQuarterTurns(state.cropQuarterTurns) % 2 != 0) {
        std::swap(width, height);
    }

    const qreal radians = std::abs(state.cropStraightenHundredths / 100.0)
        * 3.14159265358979323846 / 180.0;
    const qreal sine = std::abs(std::sin(radians));
    const qreal cosine = std::abs(std::cos(radians));
    const qreal boundingWidth = width * cosine + height * sine;
    const qreal boundingHeight = width * sine + height * cosine;
    return boundingHeight > 0.0 ? boundingWidth / boundingHeight : 1.0;
}

QRectF CropToolController::cropSceneRect(const DevelopState &state) const
{
    if (!m_window || !m_window->m_canvas || !m_window->m_canvas->scene()) {
        return {};
    }

    const QList<QGraphicsItem *> items =
        m_window->m_canvas->scene()->items(Qt::AscendingOrder);
    QGraphicsItem *imageItem = nullptr;
    for (QGraphicsItem *item : items) {
        if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
            imageItem = item;
            break;
        }
    }
    if (!imageItem) {
        return {};
    }

    const QRectF bounds = imageItem->sceneBoundingRect();
    const QRectF normalized = imaging::cropRectFromState(state);
    return QRectF(bounds.left() + normalized.left() * bounds.width(),
                  bounds.top() + normalized.top() * bounds.height(),
                  normalized.width() * bounds.width(),
                  normalized.height() * bounds.height());
}

QRectF CropToolController::fittedAspectRect(const QRectF &bounds,
                                            qreal pixelAspectRatio,
                                            const DevelopState &state) const
{
    if (bounds.isEmpty() || pixelAspectRatio <= 0.0) {
        return bounds;
    }

    const qreal boundingAspect = std::max<qreal>(0.001, rotatedBoundingAspect(state));
    const qreal normalizedRatio = pixelAspectRatio / boundingAspect;

    qreal width = bounds.width();
    qreal height = bounds.height();
    if (width / height > normalizedRatio) {
        width = height * normalizedRatio;
    } else {
        height = width / normalizedRatio;
    }

    return QRectF(bounds.center().x() - width * 0.5,
                  bounds.center().y() - height * 0.5,
                  width,
                  height);
}

QSizeF CropToolController::sourceImageSize() const
{
    if (!m_window) {
        return {};
    }
    if (!m_window->m_originalImage.isNull()) {
        return QSizeF(m_window->m_originalImage.size());
    }
    if (m_window->m_fullImageSize.isValid() && !m_window->m_fullImageSize.isEmpty()) {
        return QSizeF(m_window->m_fullImageSize);
    }
    return {};
}

void CropToolController::updateControlsFromState(const DevelopState &state)
{
    if (m_aspectCombo) {
        const QSignalBlocker blocker(m_aspectCombo);
        m_aspectCombo->setCurrentIndex(std::clamp(state.cropAspectMode, 0, 5));
    }
    if (m_straightenSlider) {
        const QSignalBlocker blocker(m_straightenSlider);
        m_straightenSlider->setValue(
            std::clamp(state.cropStraightenHundredths, -4500, 4500));
    }
    if (m_straightenValue) {
        m_straightenValue->setText(
            QStringLiteral("%1°")
                .arg(state.cropStraightenHundredths / 100.0, 0, 'f', 2));
    }
}
