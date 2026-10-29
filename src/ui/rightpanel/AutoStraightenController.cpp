#include "ui/rightpanel/AutoStraightenController.h"

#include "imaging/AutoStraighten.h"
#include "ui/canvas/PhotoCanvas.h"

#include <QEvent>
#include <QGraphicsItem>
#include <QGraphicsScene>
#include <QImage>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QSize>
#include <QSlider>
#include <QStyleOptionGraphicsItem>
#include <QVariant>
#include <QWidget>

#include <algorithm>
#include <cmath>

AutoStraightenController::AutoStraightenController(QWidget *root, QObject *parent)
    : QObject(parent)
{
    if (!root) {
        return;
    }

    m_canvas = root->findChild<PhotoCanvas *>();
    QWidget *cropPanel = root->findChild<QWidget *>(QStringLiteral("cropToolPanel"));
    if (!m_canvas || !cropPanel) {
        return;
    }

    for (QLabel *label : cropPanel->findChildren<QLabel *>()) {
        if (label && label->text() == QStringLiteral("Straighten")) {
            m_straightenLabel = label;
            break;
        }
    }

    for (QSlider *slider : cropPanel->findChildren<QSlider *>()) {
        if (slider
            && slider->orientation() == Qt::Horizontal
            && slider->minimum() == -4500
            && slider->maximum() == 4500) {
            m_straightenSlider = slider;
            break;
        }
    }

    if (m_straightenLabel && m_straightenSlider) {
        m_straightenLabel->installEventFilter(this);
    }
}

bool AutoStraightenController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_straightenLabel
        && event->type() == QEvent::MouseButtonDblClick) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton
            && mouseEvent->modifiers().testFlag(Qt::ShiftModifier)) {
            applyAutoStraighten();
            return true;
        }
    }

    return QObject::eventFilter(watched, event);
}

void AutoStraightenController::applyAutoStraighten()
{
    if (!m_canvas || !m_canvas->hasImage() || !m_straightenSlider) {
        return;
    }

    QGraphicsItem *item = imageItem();
    if (!item) {
        return;
    }

    const QImage image = analysisImage(item);
    if (image.isNull()) {
        return;
    }

    const qreal currentStraighten =
        static_cast<qreal>(m_straightenSlider->value()) / 100.0;

    int quarterTurns = 0;
    const QVariant storedQuarterTurns = m_canvas->property("apolloCropQuarterTurns");
    if (storedQuarterTurns.isValid()) {
        quarterTurns = storedQuarterTurns.toInt();
    } else {
        quarterTurns = static_cast<int>(std::llround(
            (item->rotation() - currentStraighten) / 90.0));
    }

    const imaging::AutoStraightenResult estimate =
        imaging::estimateAutoStraighten(image, quarterTurns);
    if (!estimate.valid) {
        return;
    }

    const int hundredths = std::clamp(
        static_cast<int>(std::lround(estimate.degrees * 100.0)),
        m_straightenSlider->minimum(),
        m_straightenSlider->maximum());
    m_straightenSlider->setValue(hundredths);
}

QGraphicsItem *AutoStraightenController::imageItem() const
{
    if (!m_canvas || !m_canvas->scene()) {
        return nullptr;
    }

    const auto items = m_canvas->scene()->items(Qt::AscendingOrder);
    for (QGraphicsItem *item : items) {
        if (item && item->isVisible() && !item->boundingRect().isEmpty()) {
            return item;
        }
    }
    return nullptr;
}

QImage AutoStraightenController::analysisImage(QGraphicsItem *item) const
{
    if (!item) {
        return {};
    }

    const QRectF bounds = item->boundingRect();
    if (!bounds.isValid() || bounds.isEmpty()) {
        return {};
    }

    QSize targetSize = bounds.size().toSize();
    if (!targetSize.isValid() || targetSize.isEmpty()) {
        return {};
    }
    targetSize.scale(QSize(1600, 1600), Qt::KeepAspectRatio);
    targetSize.setWidth(std::max(1, targetSize.width()));
    targetSize.setHeight(std::max(1, targetSize.height()));

    QImage image(targetSize, QImage::Format_RGB32);
    image.fill(Qt::black);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.scale(static_cast<qreal>(targetSize.width()) / bounds.width(),
                  static_cast<qreal>(targetSize.height()) / bounds.height());
    painter.translate(-bounds.left(), -bounds.top());
    QStyleOptionGraphicsItem option;
    item->paint(&painter, &option, nullptr);
    painter.end();

    return image;
}
