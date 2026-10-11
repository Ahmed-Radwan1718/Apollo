#pragma once

#include <QGraphicsView>
#include <QImage>
#include <QPointF>
#include <QSize>

class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QFocusEvent;
class QGraphicsItem;
class QGraphicsScene;
class QKeyEvent;
class QMouseEvent;
class QPaintEvent;
class QResizeEvent;
class QWheelEvent;

class PhotoCanvas final : public QGraphicsView
{
    Q_OBJECT

public:
    explicit PhotoCanvas(QWidget *parent = nullptr);
    ~PhotoCanvas() override;

    // The canvas consumes display-ready renders. Interactive previews may contain
    // fewer pixels than the full RAW while keeping the same logical photo size.
    void setImage(const QImage &image);
    void replaceImage(const QImage &image);
    void replacePreviewImage(const QImage &image, const QSize &fullResolutionSize);
    void clearImage();

    [[nodiscard]] bool hasImage() const noexcept;
    [[nodiscard]] qreal zoomPercent() const noexcept;

public slots:
    void fitToView();
    void actualPixels();
    void zoomIn();
    void zoomOut();
    void setZoomPercent(qreal percent);

signals:
    void zoomChanged(qreal percent);
    void fileDropped(const QString &filePath);

protected:
    bool event(QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void keyReleaseEvent(QKeyEvent *event) override;
    void focusOutEvent(QFocusEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    enum class ZoomMode {
        Fit,
        Manual,
    };

    [[nodiscard]] qreal currentViewScale() const noexcept;
    [[nodiscard]] qreal actualPixelScale() const noexcept;
    [[nodiscard]] qreal fitScale() const noexcept;
    [[nodiscard]] bool canPan() const noexcept;

    void setZoomPercentAt(qreal percent, const QPointF &anchorInViewport);
    void applyViewScale(qreal scale, const QPointF &anchorInViewport);
    void beginPan(const QPointF &position);
    void updatePan(const QPointF &position);
    void endPan();
    void updateCursor();

    QGraphicsScene *m_scene = nullptr;
    QGraphicsItem *m_imageItem = nullptr;

    ZoomMode m_zoomMode = ZoomMode::Fit;
    qreal m_manualZoomPercent = 100.0;

    bool m_spacePressed = false;
    bool m_panning = false;
    QPointF m_lastPanPosition;

    static constexpr qreal kMinimumZoomPercent = 1.0;
    static constexpr qreal kMaximumZoomPercent = 1600.0;
    static constexpr qreal kZoomStep = 1.25;
    static constexpr qreal kFitMargin = 24.0;
};
