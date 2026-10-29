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
        const QSizeF nextLogicalSize =
            logicalSize.isValid() && !logicalSize.isEmpty()
                ? logicalSize
                : QSizeF(image.size());
        const bool preserveVisibleRect =
            !m_image.isNull()
            && m_logicalSize == nextLogicalSize
            && m_visibleLogicalRect.isValid()
            && !m_visibleLogicalRect.isEmpty();

        prepareGeometryChange();
        m_image = image;
        m_image.setDevicePixelRatio(1.0);
        m_logicalSize = nextLogicalSize;

        if (!preserveVisibleRect) {
            resetVisibleLogicalRect();
        } else {
            setVisibleLogicalRect(m_visibleLogicalRect);
        }

        update();
    }

    [[nodiscard]] const QImage &image() const noexcept { return m_image; }

    [[nodiscard]] const QRectF &visibleLogicalRect() const noexcept
    {
        return m_visibleLogicalRect;
    }

    void resetVisibleLogicalRect()
    {
        m_visibleLogicalRect = boundingRect();
        update();
    }

    void setVisibleLogicalRect(const QRectF &requestedRect)
    {
        const QRectF fullRect = boundingRect();
        if (fullRect.isEmpty()) {
            m_visibleLogicalRect = {};
            update();
            return;
        }

        if (!requestedRect.isValid() || requestedRect.isEmpty()) {
            m_visibleLogicalRect = fullRect;
            update();
            return;
        }

        const qreal width = std::clamp(requestedRect.width(),
                                       1.0,
                                       fullRect.width());
        const qreal height = std::clamp(requestedRect.height(),
                                        1.0,
                                        fullRect.height());
        const qreal x = std::clamp(requestedRect.x(),
                                   fullRect.left(),
                                   fullRect.right() - width);
        const qreal y = std::clamp(requestedRect.y(),
                                   fullRect.top(),
                                   fullRect.bottom() - height);

        m_visibleLogicalRect = QRectF(x, y, width, height);
        update();
    }

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

        const QRectF fullRect = boundingRect();
        if (fullRect.isEmpty()) {
            return;
        }

        const QRectF visibleRect =
            m_visibleLogicalRect.isValid() && !m_visibleLogicalRect.isEmpty()
                ? m_visibleLogicalRect
                : fullRect;
        const qreal sourceScaleX =
            static_cast<qreal>(m_image.width()) / fullRect.width();
        const qreal sourceScaleY =
            static_cast<qreal>(m_image.height()) / fullRect.height();
        const QRectF sourceRect(
            (visibleRect.left() - fullRect.left()) * sourceScaleX,
            (visibleRect.top() - fullRect.top()) * sourceScaleY,
            visibleRect.width() * sourceScaleX,
            visibleRect.height() * sourceScaleY);

        painter->setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter->drawImage(fullRect, m_image, sourceRect);
    }

private:
    QImage m_image;
    QSizeF m_logicalSize;
    QRectF m_visibleLogicalRect;
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

QRectF centeredViewportRect(const QSizeF &size, const QRectF &viewportRect)
{
    return QRectF(viewportRect.center().x() - (size.width() * 0.5),
                  viewportRect.center().y() - (size.height() * 0.5),
                  size.width(),
                  size.height());
}

QPointF normalizedPointInFrame(const QPointF &point, const QRectF &frame)
{
    if (frame.width() <= 0.0 || frame.height() <= 0.0) {
        return QPointF(0.5, 0.5);
    }

    return QPointF(
        std::clamp((point.x() - frame.left()) / frame.width(), 0.0, 1.0),
        std::clamp((point.y() - frame.top()) / frame.height(), 0.0, 1.0));
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

    if (m_zoomMode == ZoomMode::Manual) {
        return m_manualZoomPercent;
    }

    const qreal oneToOne = actualPixelScale();
    if (oneToOne <= 0.0) {
        return 0.0;
    }

    return (fitScale() / oneToOne) * 100.0;
}

void PhotoCanvas::fitToView()
{
    if (!hasImage()) {
        return;
    }

    m_zoomMode = ZoomMode::Fit;
    asPhotoItem(m_imageItem)->resetVisibleLogicalRect();

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

    if (!hasImage()) {
        updateCursor();
        return;
    }

    if (m_zoomMode == ZoomMode::Fit) {
        fitToView();
    } else {
        setZoomPercentAt(m_manualZoomPercent, QRectF(viewport()->rect()).center());
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
        const bool hasLocalFile = std::any_of(
            urls.cbegin(), urls.cend(),
            [](const QUrl &url) { return url.isLocalFile(); });
        if (hasLocalFile) {
            event->acceptProposedAction();
            return;
        }
    }

    event->ignore();
}

void PhotoCanvas::dropEvent(QDropEvent *event)
{
    const QList<QUrl> urls = event->mimeData()->urls();
    bool openedLocalFile = false;

    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            continue;
        }

        emit fileDropped(QFileInfo(url.toLocalFile()).absoluteFilePath());
        openedLocalFile = true;
    }

    if (!openedLocalFile) {
        event->ignore();
        return;
    }

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

    const qreal availableWidth =
        std::max<qreal>(1.0, viewport()->width() - (2.0 * kFitMargin));
    const qreal availableHeight =
        std::max<qreal>(1.0, viewport()->height() - (2.0 * kFitMargin));

    return std::min(availableWidth / imageRect.width(),
                    availableHeight / imageRect.height());
}

bool PhotoCanvas::canPan() const noexcept
{
    if (!hasImage()) {
        return false;
    }

    const PhotoImageItem *item = asPhotoItem(m_imageItem);
    const QRectF fullRect = item->boundingRect();
    const QRectF visibleRect = item->visibleLogicalRect();

    return visibleRect.width() < fullRect.width() - 0.5
        || visibleRect.height() < fullRect.height() - 0.5;
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

    PhotoImageItem *item = asPhotoItem(m_imageItem);
    const QRectF fullRect = item->boundingRect();
    if (fullRect.isEmpty()) {
        return;
    }

    const QRectF viewportRect(viewport()->rect());
    const qreal currentScale = std::max<qreal>(currentViewScale(), 1e-9);
    const QRectF currentFrame = centeredViewportRect(
        QSizeF(fullRect.width() * currentScale,
               fullRect.height() * currentScale),
        viewportRect);
    const QPointF currentNormalized =
        normalizedPointInFrame(anchorInViewport, currentFrame);
    const QRectF currentVisible = item->visibleLogicalRect().isEmpty()
        ? fullRect
        : item->visibleLogicalRect();
    const QPointF logicalAnchor(
        currentVisible.left() + currentNormalized.x() * currentVisible.width(),
        currentVisible.top() + currentNormalized.y() * currentVisible.height());

    const qreal defaultScale = fitScale();
    QTransform transform;

    if (scale <= defaultScale) {
        item->resetVisibleLogicalRect();
        transform.scale(scale, scale);
        setTransform(transform);
        centerOn(m_imageItem);
    } else {
        const qreal cropRatio = defaultScale / scale;
        const QSizeF cropSize(fullRect.width() * cropRatio,
                              fullRect.height() * cropRatio);
        const QRectF fixedFrame = centeredViewportRect(
            QSizeF(fullRect.width() * defaultScale,
                   fullRect.height() * defaultScale),
            viewportRect);
        const QPointF targetNormalized =
            normalizedPointInFrame(anchorInViewport, fixedFrame);

        item->setVisibleLogicalRect(
            QRectF(logicalAnchor.x() - targetNormalized.x() * cropSize.width(),
                   logicalAnchor.y() - targetNormalized.y() * cropSize.height(),
                   cropSize.width(),
                   cropSize.height()));

        transform.scale(defaultScale, defaultScale);
        setTransform(transform);
        centerOn(m_imageItem);
    }

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
    if (!canPan()) {
        return;
    }

    const QPointF delta = position - m_lastPanPosition;
    PhotoImageItem *item = asPhotoItem(m_imageItem);
    QRectF visibleRect = item->visibleLogicalRect();
    const QRectF fullRect = item->boundingRect();
    const qreal frameWidth = fullRect.width() * currentViewScale();
    const qreal frameHeight = fullRect.height() * currentViewScale();

    if (frameWidth > 0.0 && frameHeight > 0.0) {
        visibleRect.translate(
            -delta.x() * (visibleRect.width() / frameWidth),
            -delta.y() * (visibleRect.height() / frameHeight));
        item->setVisibleLogicalRect(visibleRect);
        viewport()->update();
    }

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