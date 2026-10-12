#include "ui/rightpanel/WhiteBalancePickerController.h"

#include "ui/canvas/PhotoCanvas.h"
#include "ui/rightpanel/BasicPanel.h"

#include <QCursor>
#include <QEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QResizeEvent>
#include <QSignalBlocker>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>

namespace {

class WhiteBalancePickerButton final : public QToolButton
{
public:
    explicit WhiteBalancePickerButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setFixedSize(22, 18);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Pick neutral white balance"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (isChecked()) {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(54, 54, 54));
            painter.drawRoundedRect(rect().adjusted(1, 1, -1, -1), 3.0, 3.0);
        }

        QColor color;
        if (!isEnabled()) {
            color = QColor(88, 88, 88);
        } else if (underMouse() || isChecked()) {
            color = QColor(245, 245, 245);
        } else {
            color = QColor(177, 177, 177);
        }

        painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.setBrush(Qt::NoBrush);
        painter.translate(width() / 2.0, height() / 2.0);
        painter.rotate(-45.0);

        // Familiar eyedropper silhouette: rounded squeeze bulb, collar,
        // tapered glass tube, and a pointed sampling tip.
        painter.drawRoundedRect(QRectF(-3.1, -6.7, 6.2, 5.0), 1.5, 1.5);
        painter.drawLine(QPointF(-2.7, -1.3), QPointF(2.7, -1.3));
        painter.drawLine(QPointF(-1.75, -1.3), QPointF(-1.05, 3.6));
        painter.drawLine(QPointF(1.75, -1.3), QPointF(1.05, 3.6));
        painter.drawLine(QPointF(-1.05, 3.6), QPointF(0.0, 6.5));
        painter.drawLine(QPointF(1.05, 3.6), QPointF(0.0, 6.5));
    }
};

QCursor whiteBalancePickerCursor()
{
    QPixmap pixmap(24, 24);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    painter.setPen(QPen(QColor(20, 20, 20, 220), 3.0,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(7.0, 7.0), QPointF(15.0, 15.0));
    painter.drawLine(QPointF(14.0, 5.0), QPointF(18.0, 9.0));
    painter.drawLine(QPointF(6.0, 16.0), QPointF(4.5, 19.0));

    painter.setPen(QPen(QColor(238, 238, 238), 1.4,
                        Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.drawLine(QPointF(7.0, 7.0), QPointF(15.0, 15.0));
    painter.drawLine(QPointF(14.0, 5.0), QPointF(18.0, 9.0));
    painter.drawLine(QPointF(6.0, 16.0), QPointF(4.5, 19.0));

    return QCursor(pixmap, 5, 19);
}

} // namespace

WhiteBalancePickerController::WhiteBalancePickerController(BasicPanel *panel,
                                                           PhotoCanvas *canvas,
                                                           QObject *parent)
    : QObject(parent)
    , m_canvas(canvas)
{
    installButton(panel);

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        m_canvas->viewport()->installEventFilter(this);
    }

    setEnabled(false);
}

void WhiteBalancePickerController::setEnabled(bool enabled)
{
    if (!enabled) {
        setActive(false);
    }

    if (m_button) {
        m_button->setEnabled(enabled);
    }
}

void WhiteBalancePickerController::setActive(bool active)
{
    if (!m_canvas || !m_button || (active && !m_button->isEnabled())) {
        active = false;
    }

    if (m_active == active) {
        if (m_button) {
            const QSignalBlocker blocker(m_button);
            m_button->setChecked(active);
        }
        return;
    }

    if (active) {
        m_previousCursorShape = m_canvas->viewport()->cursor().shape();
    }

    m_active = active;

    {
        const QSignalBlocker blocker(m_button);
        m_button->setChecked(m_active);
    }

    if (m_active) {
        m_canvas->setFocus(Qt::OtherFocusReason);
    }

    updateCursor();
}

bool WhiteBalancePickerController::isActive() const noexcept
{
    return m_active;
}

bool WhiteBalancePickerController::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_hostLabel && event->type() == QEvent::Resize) {
        positionButton();
    }

    if (!m_active || !m_canvas) {
        return QObject::eventFilter(watched, event);
    }

    if (watched == m_canvas && event->type() == QEvent::KeyPress) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            setActive(false);
            return true;
        }
        if (keyEvent->key() == Qt::Key_Space) {
            return true;
        }
    }

    if (watched == m_canvas && event->type() == QEvent::KeyRelease) {
        const auto *keyEvent = static_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Space) {
            return true;
        }
    }

    if (watched == m_canvas->viewport()
        && event->type() == QEvent::MouseButtonPress) {
        const auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() != Qt::LeftButton) {
            return QObject::eventFilter(watched, event);
        }

        if (!m_canvas->hasImage()) {
            return true;
        }

        const QPointF scenePoint = m_canvas->mapToScene(mouseEvent->position().toPoint());
        const QRectF imageRect = m_canvas->sceneRect();
        if (imageRect.isEmpty() || !imageRect.contains(scenePoint)) {
            return true;
        }

        if (!sampledPatchLooksUsable(mouseEvent->position())) {
            QToolTip::showText(
                m_canvas->viewport()->mapToGlobal(mouseEvent->position().toPoint()),
                tr("Choose a neutral midtone that is not clipped or too dark."),
                m_canvas->viewport());
            return true;
        }

        const qreal normalizedX = std::clamp(
            (scenePoint.x() - imageRect.left()) / imageRect.width(), 0.0, 1.0);
        const qreal normalizedY = std::clamp(
            (scenePoint.y() - imageRect.top()) / imageRect.height(), 0.0, 1.0);

        setActive(false);
        emit pointPicked(QPointF(normalizedX, normalizedY));
        return true;
    }

    return QObject::eventFilter(watched, event);
}

void WhiteBalancePickerController::installButton(BasicPanel *panel)
{
    if (!panel) {
        return;
    }

    const auto sectionLabels =
        panel->findChildren<QLabel *>(QStringLiteral("basicSectionLabel"));
    if (sectionLabels.isEmpty()) {
        return;
    }

    m_hostLabel = sectionLabels.constFirst();
    m_hostLabel->installEventFilter(this);

    auto *button = new WhiteBalancePickerButton(m_hostLabel);
    button->setObjectName(QStringLiteral("whiteBalancePickerButton"));
    m_button = button;

    positionButton();
    m_button->show();
    m_button->raise();

    connect(m_button, &QToolButton::toggled,
            this, [this](bool checked) { setActive(checked); });
}

void WhiteBalancePickerController::positionButton()
{
    if (!m_hostLabel || !m_button) {
        return;
    }

    const int x = std::max(0, m_hostLabel->width() - m_button->width());
    const int y = std::max(0, (m_hostLabel->height() - m_button->height()) / 2);
    m_button->move(x, y);
    m_button->raise();
}

bool WhiteBalancePickerController::sampledPatchLooksUsable(
    const QPointF &viewportPosition) const
{
    if (!m_canvas || !m_canvas->viewport()) {
        return false;
    }

    constexpr int kSampleSide = 9;
    constexpr int kHalfSample = kSampleSide / 2;

    const QPoint center = viewportPosition.toPoint();
    QRect sampleRect(center.x() - kHalfSample,
                     center.y() - kHalfSample,
                     kSampleSide,
                     kSampleSide);
    sampleRect = sampleRect.intersected(m_canvas->viewport()->rect());
    if (sampleRect.isEmpty()) {
        return false;
    }

    const QImage sample = m_canvas->viewport()->grab(sampleRect)
                              .toImage()
                              .convertToFormat(QImage::Format_RGB32);
    if (sample.isNull()) {
        return false;
    }

    double luminanceSum = 0.0;
    double maximumChannelSum = 0.0;
    qsizetype pixelCount = 0;

    for (int y = 0; y < sample.height(); ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(sample.constScanLine(y));
        for (int x = 0; x < sample.width(); ++x) {
            const QRgb pixel = line[x];
            const int red = qRed(pixel);
            const int green = qGreen(pixel);
            const int blue = qBlue(pixel);
            luminanceSum += 0.2126 * red + 0.7152 * green + 0.0722 * blue;
            maximumChannelSum += std::max(red, std::max(green, blue));
            ++pixelCount;
        }
    }

    if (pixelCount <= 0) {
        return false;
    }

    const double meanLuminance = luminanceSum / static_cast<double>(pixelCount);
    const double meanMaximum = maximumChannelSum / static_cast<double>(pixelCount);
    return meanLuminance >= 8.0 && meanMaximum <= 248.0;
}

void WhiteBalancePickerController::updateCursor()
{
    if (!m_canvas || !m_canvas->viewport()) {
        return;
    }

    if (m_active) {
        m_canvas->viewport()->setCursor(whiteBalancePickerCursor());
    } else {
        m_canvas->viewport()->setCursor(m_previousCursorShape);
    }
}
