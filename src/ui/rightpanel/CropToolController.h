#pragma once

#include "app/DevelopState.h"

#include <QObject>
#include <QRectF>
#include <QSizeF>
#include <QString>

class MainWindow;
class QComboBox;
class QEvent;
class QLabel;
class QSlider;
class QToolButton;
class QWidget;
class CropOverlayWidget;

class CropToolController final : public QObject
{
public:
    explicit CropToolController(MainWindow *window, QObject *parent = nullptr);

    // Shared by Transform so all geometry is previewed through the same canvas
    // owner instead of competing QGraphicsItem transforms.
    void previewGeometryState(const DevelopState &state, bool forceFit = false);
    void refreshGeometryFromWindowState(bool forceFit = false);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void buildUi();
    void scheduleSync();
    void syncFromWindowState();
    void beginEditing();
    void finishEditing(bool commitChanges);
    void commitWorkingStateForPath(const QString &filePath);

    void applyGeometryToCanvas(const DevelopState &state, bool editing, bool forceFit = false);
    void fitSceneRect(const QRectF &sceneRect, int marginPixels);
    void updateOverlay();
    void refreshNavigatorPreview(const DevelopState &state);

    void rotateByQuarterTurns(int delta);
    void setStraighten(int hundredths);
    void setAspectMode(int mode);
    void resetWorkingGeometry();
    void resetCropForCurrentGeometry();
    void fitCurrentCropToAspect();

    [[nodiscard]] qreal selectedAspectRatio(const DevelopState &state) const;
    [[nodiscard]] qreal rotatedBoundingAspect(const DevelopState &state) const;
    [[nodiscard]] QRectF cropSceneRect(const DevelopState &state) const;
    [[nodiscard]] QRectF fittedAspectRect(const QRectF &bounds,
                                          qreal pixelAspectRatio,
                                          const DevelopState &state) const;
    [[nodiscard]] QSizeF sourceImageSize() const;
    void updateControlsFromState(const DevelopState &state);

    MainWindow *m_window = nullptr;
    QWidget *m_toolStrip = nullptr;
    QToolButton *m_selectorButton = nullptr;
    QWidget *m_panel = nullptr;
    QComboBox *m_aspectCombo = nullptr;
    QToolButton *m_rotateLeftButton = nullptr;
    QToolButton *m_rotateRightButton = nullptr;
    QSlider *m_straightenSlider = nullptr;
    QLabel *m_straightenValue = nullptr;
    QToolButton *m_resetButton = nullptr;
    QToolButton *m_doneButton = nullptr;
    CropOverlayWidget *m_overlay = nullptr;

    DevelopState m_workingState;
    DevelopState m_editStartState;
    DevelopState m_appliedState;
    QString m_workingFilePath;

    bool m_editing = false;
    bool m_syncScheduled = false;
    bool m_applyingView = false;
    bool m_historyNavigationPending = false;
    bool m_haveAppliedState = false;
};
