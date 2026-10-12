#include "ui/canvas/PhotoCanvas.h"

#include "ui/canvas/BeforeAfterToggle.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFocusEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QKeyEvent>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QScrollBar>
#include <QSizeF>
#include <QUrl>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>

namespace {

class PhotoImageItem final : public QGraphicsItem
{
public:
    void setImage(const QImage &image, const QSizeF &logicalSize)
    {
        prepareGeometryChange();
        m_image = image;
        m_image.setDevicePixelRatio(1.0);
        m_logicalSize = logicalSize.isValid() && !logicalSize.isEmpty()
            ? logicalSize
            : QSizeF(image.size());
        update();
    }

    [[nodiscard]] const QImage &image() const noexcept { return m_image; }

    QRectF boundingRect() const override
    {
        return QRectF(QPointF(0.0, 0.0), m_logicalSize);
    }

    void paint(QPainter *painter,
               const QStyleOptionGraphicsItem *,
               QWidget *) override
    {
        if (m_image.isNull()) {
            return;
        }

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(boundingRect(), m_image, QRectF(m_image.rect()));
    }

private:
    QImage m_image;
    QSizeF m_logicalSize;
};

PhotoImageItem *asPhotoItem(QGraphicsItem *item)
{
    return static_cast<PhotoImageItem *>(item);
}

BeforeAfterToggle *beforeAfterToggle(const PhotoCanvas *canvas)
{
    if (!canvas || !canvas->viewport()) {
        return nullptr;
    }

    QWidget *widget = canvas->viewport()->findChild<QWidget *>(
        QStringLiteral("beforeAfterToggle"), Qt::FindDirectChildrenOnly);
    return static_cast<BeforeAfterToggle *>(widget);
}

void positionBeforeAfterToggle(PhotoCanvas *canvas)
{
    BeforeAfterToggle *toggle = beforeAfterToggle(canvas);
    if (!toggle || !canvas || !canvas->viewport()) {
        return;
    }

    constexpr int rightMargin = 12;
    constexpr int bottomMargin = 6;
    toggle->move(qMax(0, canvas->viewport()->width() - toggle->width() - rightMargin),
                 qMax(0, canvas->viewport()->height() - toggle->height() - bottomMargin));
    toggle->raise();
}

} // namespace

PhotoCanvas::PhotoCanvas(QWidget *parent)
    : QGraphicsView(parent)
    , m_scene(new QGraphicsScene(this))
    , m_imageItem(new PhotoImageItem())
{
    m_scene->addItem(m_imageItem);
    setScene(m_scene);

    setFrameShape(QFrame::NoFrame);
    setAlignment(Qt::AlignCenter);
    setBackgroundBrush(QColor(24, 24, 24));
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTransformationAnchor(QGraphicsView::NoAnchor);
    setResizeAnchor(QGraphicsView::NoAnchor);
    setRenderHint(QPainter::SmoothPixmapTransform, true);
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAcceptDrops(true);
    viewport()->setAcceptDrops(true);

    auto *toggle = new BeforeAfterToggle(viewport());
    toggle->setDisplayCallback(
        [this](const QImage &image, const QSizeF &logicalSize) {
            if (image.isNull()) {
                return;
            }

            const PhotoImageItem *currentItem = asPhotoItem(m_imageItem);
            const bool preserveView = hasImage()
                && currentItem->boundingRect().size() == logicalSize;

            asPhotoItem(m_imageItem)->setImage(image, logicalSize);
            m_scene->setSceneRect(m_imageItem->boundingRect());
            m_imageItem->setVisible(true);

            if (!preserveView) {
                m_zoomMode = ZoomMode::Fit;
                fitToView();
            } else {
                viewport()->update();
                updateCursor();
            }
        });
    positionBeforeAfterToggle(this);

    updateCursor();
}

PhotoCanvas::~PhotoCanvas() = default;

void PhotoCanvas::setImage(const QImage &image)
{
    if (image.isNull()) {
        clearImage();
        return;
    }

    if (BeforeAfterToggle *toggle = beforeAfterToggle(this)) {
        toggle->setOriginalImage(image);
    }

    asPhotoItem(m_imageItem)->setImage(image, QSizeF(image.size()));
    m_scene->setSceneRect(m_imageItem->boundingRect());
    m_imageItem->setVisible(true);

    m_zoomMode = ZoomMode::Fit;
    fitToView();
    viewport()->update();
    updateCursor();
}

void PhotoCanvas::replaceImage(const QImage &image)
{
    if (image.isNull()) {
        clearImage();
        return;
    }

    if (BeforeAfterToggle *toggle = beforeAfterToggle(this)) {
        toggle->setCurrentImage(image, QSizeF(image.size()));
        if (toggle->showingBefore()) {
            return;
        }
    }

    const PhotoImageItem *currentItem = asPhotoItem(m_imageItem);
    if (!hasImage() || currentItem->boundingRect().size() != QSizeF(image.size())) {
        asPhotoItem(m_imageItem)->setImage(image, QSizeF(image.size()));
        m_scene->setSceneRect(m_imageItem->boundingRect());
        m_imageItem->setVisible(true);
        m_zoomMode = ZoomMode::Fit;
        fitToView();
        viewport()->update();
        updateCursor();
        return;
    }

    asPhotoItem(m_imageItem)->setImage(image, QSizeF(image.size()));
    m_scene->setSceneRect(m_imageItem->boundingRect());
    m_imageItem->setVisible(true);
    viewport()->update();
    updateCursor();
}

void PhotoCanvas::replacePreviewImage(const QImage &image,
                                      const QSize &fullResolutionSize)
{
    if (image.isNull() || !fullResolutionSize.isValid() || fullResolutionSize.isEmpty()) {
        return;
    }

    const QSizeF logicalSize(fullResolutionSize);
    if (BeforeAfterToggle *toggle = beforeAfterToggle(this)) {
        toggle->setCurrentImage(image, logicalSize);
        if (toggle->showingBefore()) {
            return;
        }
    }

    const PhotoImageItem *currentItem = asPhotoItem(m_imageItem);
    const bool preserveView = hasImage()
        && currentItem->boundingRect().size() == logicalSize;

    asPhotoItem(m_imageItem)->setImage(image, logicalSize);
    m_scene->setSceneRect(m_imageItem->boundingRect());
    m_imageItem->setVisible(true);

    if (!preserveView) {
        m_zoomMode = ZoomMode::Fit;
        fitToView();
    } else {
        viewport()->update();
        updateCursor();
    }
}

void PhotoCanvas::clearImage()
{
    if (BeforeAfterToggle *toggle = beforeAfterToggle(this)) {
        toggle->clearImages();
    }

    asPhotoItem(m_imageItem)->setImage({}, {});
    m_imageItem->setVisible(false);
    m_scene->setSceneRect({});
    resetTransform();
    m_zoomMode = ZoomMode::Fit;
    m_manualZoomPercent = 100.0;
    viewport()->update();
    updateCursor();
}

bool PhotoCanvas::hasImage() const noexcept
{
    return !asPhotoItem(m_imageItem)->image().isNull();
}

qreal PhotoCanvas::zoomPercent() const noexcept
{
    if (!hasImage()) {
        return 0.0;
    }

    const qreal oneToOne = actualPixelScale();
    if (oneToOne <= 0.0) {
        return 0.0;
    }

    return (currentViewScale() / oneToOne) * 100.0;
}

void PhotoCanvas::fitToView()
{
    if (!hasImage()) {
        return;
    }

    m_zoomMode = ZoomMode::Fit;

    const qreal scale = fitScale();
    QTransform transform;
    transform.scale(scale, scale);
    setTransform(transform);
    centerOn(m_imageItem);

    emit zoomChanged(zoomPercent());
    updateCursor();
}

void PhotoCanvas::actualPixels()
{
    if (!hasImage()) {
        return;
    }

    setZoomPercentAt(100.0, QRectF(viewport()->rect()).center());
}

void PhotoCanvas::zoomIn()
{
    if (!hasImage()) {
        return;
    }

    const qreal basePercent = (m_zoomMode == ZoomMode::Fit)
        ? zoomPercent()
        : m_manualZoomPercent;
    setZoomPercentAt(basePercent * kZoomStep, QRectF(viewport()->rect()).center());
}

void PhotoCanvas::zoomOut()
{
    if (!hasImage()) {
        return;
    }

    const qreal basePercent = (m_zoomMode == ZoomMode::Fit)
        ? zoomPercent()
        : m_manualZoomPercent;
    setZoomPercentAt(basePercent / kZoomStep, QRectF(viewport()->rect()).center());
}

void PhotoCanvas::setZoomPercent(qreal percent)
{
    setZoomPercentAt(percent, QRectF(viewport()->rect()).center());
}

bool PhotoCanvas::event(QEvent *event)
{
    const bool handled = QGraphicsView::event(event);

    if (event->type() == QEvent::DevicePixelRatioChange && hasImage()) {
        if (m_zoomMode == ZoomMode::Fit) {
            fitToView();
        } else {
            // Keep the semantic zoom level stable when the window moves between
            // monitors with different Windows scaling factors.
            setZoomPercentAt(m_manualZoomPercent, QRectF(viewport()->rect()).center());
        }
    }

    return handled;
}

void PhotoCanvas::paintEvent(QPaintEvent *event)
{
    QGraphicsView::paintEvent(event);

    if (hasImage()) {
        return;
    }

    QPainter painter(viewport());
    painter.setRenderHint(QPainter::TextAntialiasing, true);
    painter.setPen(QColor(150, 150, 150));

    QFont font = painter.font();
    font.setPointSizeF(font.pointSizeF() + 1.0);
    painter.setFont(font);

    painter.drawText(viewport()->rect(),
                     Qt::AlignCenter,
                     tr("Drop a photo here\nor use File > Open"));
}

void PhotoCanvas::resizeEvent(QResizeEvent *event)
{
    QGraphicsView::resizeEvent(event);
    positionBeforeAfterToggle(this);

    if (hasImage() && m_zoomMode == ZoomMode::Fit) {
        fitToView();
    } else {
        updateCursor();
    }
}

void PhotoCanvas::wheelEvent(QWheelEvent *event)
{
    if (!hasImage()) {
        event->ignore();
        return;
    }

    const int delta = event->angleDelta().y();
    if (delta == 0) {
        event->ignore();
        return;
    }

    const qreal steps = static_cast<qreal>(delta) / 120.0;
    const qreal factor = std::pow(kZoomStep, steps);
    const qreal basePercent = (m_zoomMode == ZoomMode::Fit)
        ? zoomPercent()
        : m_manualZoomPercent;

    setZoomPercentAt(basePercent * factor, event->position());
    event->accept();
}

void PhotoCanvas::mousePressEvent(QMouseEvent *event)
{
    const bool middlePan = event->button() == Qt::MiddleButton;
    const bool spacePan = event->button() == Qt::LeftButton && m_spacePressed;
    const bool directPan = event->button() == Qt::LeftButton
        && canPan()
        && itemAt(event->position().toPoint()) == m_imageItem;

    if (hasImage() && canPan() && (middlePan || spacePan || directPan)) {
        beginPan(event->position());
        event->accept();
        return;
    }

    QGraphicsView::mousePressEvent(event);
}

void PhotoCanvas::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        updatePan(event->position());
        event->accept();
        return;
    }

    QGraphicsView::mouseMoveEvent(event);
}

void PhotoCanvas::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_panning
        && (event->button() == Qt::MiddleButton || event->button() == Qt::LeftButton)) {
        endPan();
        event->accept();
        return;
    }

    QGraphicsView::mouseReleaseEvent(event);
}

void PhotoCanvas::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (!hasImage() || event->button() != Qt::LeftButton) {
        QGraphicsView::mouseDoubleClickEvent(event);
        return;
    }

    if (m_zoomMode == ZoomMode::Fit) {
        setZoomPercentAt(100.0, event->position());
    } else {
        fitToView();
    }

    event->accept();
}

void PhotoCanvas::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = true;
        updateCursor();
        event->accept();
        return;
    }

    QGraphicsView::keyPressEvent(event);
}

void PhotoCanvas::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat()) {
        m_spacePressed = false;
        if (m_panning) {
            endPan();
        }
        updateCursor();
        event->accept();
        return;
    }

    QGraphicsView::keyReleaseEvent(event);
}

void PhotoCanvas::focusOutEvent(QFocusEvent *event)
{
    m_spacePressed = false;
    if (m_panning) {
        endPan();
    }
    updateCursor();
    QGraphicsView::focusOutEvent(event);
}

void PhotoCanvas::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        const QList<QUrl> urls = event->mimeData()->urls();
        if (!urls.isEmpty() && urls.first().isLocalFile()) {
            event->acceptProposedAction();
            return;
        }
    }

    event->ignore();
}

void PhotoCanvas::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    if (urls.isEmpty() || !urls.first().isLocalFile()) {
        event->ignore();
        return;
    }

    emit fileDropped(QFileInfo(urls.first().toLocalFile()).absoluteFilePath());
    event->acceptProposedAction();
}

qreal PhotoCanvas::currentViewScale() const noexcept
{
    return transform().m11();
}

qreal PhotoCanvas::actualPixelScale() const noexcept
{
    const qreal dpr = viewport()->devicePixelRatioF();
    return dpr > 0.0 ? (1.0 / dpr) : 1.0;
}

qreal PhotoCanvas::fitScale() const noexcept
{
    if (!hasImage()) {
        return 1.0;
    }

    const QRectF imageRect = m_imageItem->boundingRect();
    if (imageRect.isEmpty()) {
        return 1.0;
    }

    const qreal availableWidth = std::max<qreal>(1.0, viewport()->width() - (2.0 * kFitMargin));
    const qreal availableHeight = std::max<qreal>(1.0, viewport()->height() - (2.0 * kFitMargin));

    return std::min(availableWidth / imageRect.width(),
                    availableHeight / imageRect.height());
}

bool PhotoCanvas::canPan() const noexcept
{
    if (!hasImage()) {
        return false;
    }

    const qreal scale = currentViewScale();
    const QRectF imageRect = m_imageItem->boundingRect();
    const qreal scaledWidth = imageRect.width() * scale;
    const qreal scaledHeight = imageRect.height() * scale;

    return scaledWidth > viewport()->width() + 0.5
        || scaledHeight > viewport()->height() + 0.5;
}

void PhotoCanvas::setZoomPercentAt(qreal percent, const QPointF &anchorInViewport)
{
    if (!hasImage()) {
        return;
    }

    const qreal clampedPercent = std::clamp(percent,
                                            kMinimumZoomPercent,
                                            kMaximumZoomPercent);

    m_zoomMode = ZoomMode::Manual;
    m_manualZoomPercent = clampedPercent;

    applyViewScale(actualPixelScale() * (clampedPercent / 100.0),
                   anchorInViewport);
}

void PhotoCanvas::applyViewScale(qreal scale, const QPointF &anchorInViewport)
{
    if (!hasImage() || scale <= 0.0) {
        return;
    }

    const QPoint anchorPoint = anchorInViewport.toPoint();
    const QPointF anchoredScenePoint = mapToScene(anchorPoint);

    QTransform newTransform;
    newTransform.scale(scale, scale);
    setTransform(newTransform);

    const QPointF viewportCenter = QRectF(viewport()->rect()).center();
    const QPointF offsetFromCenter = anchorInViewport - viewportCenter;
    const QPointF desiredSceneCenter(
        anchoredScenePoint.x() - (offsetFromCenter.x() / scale),
        anchoredScenePoint.y() - (offsetFromCenter.y() / scale));

    centerOn(desiredSceneCenter);

    emit zoomChanged(zoomPercent());
    updateCursor();
}

void PhotoCanvas::beginPan(const QPointF &position)
{
    m_panning = true;
    m_lastPanPosition = position;
    viewport()->setCursor(Qt::ClosedHandCursor);
}

void PhotoCanvas::updatePan(const QPointF &position)
{
    const QPointF delta = position - m_lastPanPosition;

    auto *horizontal = horizontalScrollBar();
    auto *vertical = verticalScrollBar();

    horizontal->setValue(horizontal->value() - static_cast<int>(std::lround(delta.x())));
    vertical->setValue(vertical->value() - static_cast<int>(std::lround(delta.y())));

    m_lastPanPosition = position;
}

void PhotoCanvas::endPan()
{
    m_panning = false;
    updateCursor();
}

void PhotoCanvas::updateCursor()
{
    if (m_panning) {
        viewport()->setCursor(Qt::ClosedHandCursor);
        return;
    }

    if (hasImage() && (m_spacePressed || canPan())) {
        viewport()->setCursor(Qt::OpenHandCursor);
        return;
    }

    viewport()->setCursor(Qt::ArrowCursor);
}
