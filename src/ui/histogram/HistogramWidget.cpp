#include "ui/histogram/HistogramWidget.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QSizePolicy>

#include <array>

namespace {

QPainterPath histogramPath(const std::array<quint64, imaging::HistogramData::kBinCount> &bins,
                           quint64 peak,
                           const QRectF &rect)
{
    QPainterPath path;
    if (peak == 0 || rect.isEmpty()) {
        return path;
    }

    path.moveTo(rect.left(), rect.bottom());

    constexpr qreal lastBin = imaging::HistogramData::kBinCount - 1.0;
    for (int bin = 0; bin < imaging::HistogramData::kBinCount; ++bin) {
        const qreal x = rect.left()
            + (static_cast<qreal>(bin) / lastBin) * rect.width();
        const qreal normalized = static_cast<qreal>(bins[static_cast<std::size_t>(bin)])
            / static_cast<qreal>(peak);
        const qreal y = rect.bottom() - (normalized * rect.height());
        path.lineTo(x, y);
    }

    path.lineTo(rect.right(), rect.bottom());
    path.closeSubpath();
    return path;
}

} // namespace

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(160);
}

void HistogramWidget::setImage(const QImage &image)
{
    m_histogram = imaging::Histogram::calculate(image);
    update();
}

void HistogramWidget::clear()
{
    m_histogram = {};
    update();
}

QSize HistogramWidget::sizeHint() const
{
    return {290, 180};
}

QSize HistogramWidget::minimumSizeHint() const
{
    return {220, 150};
}

void HistogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(28, 28, 28));

    const QRectF plotRect = QRectF(rect()).adjusted(8.0, 8.0, -8.0, -8.0);
    painter.fillRect(plotRect, Qt::black);

    if (m_histogram.isEmpty()) {
        painter.setPen(QColor(125, 125, 125));
        painter.drawText(plotRect, Qt::AlignCenter, QStringLiteral("No image"));
        painter.setPen(QColor(58, 58, 58));
        painter.drawRect(plotRect);
        return;
    }

    const quint64 peak = m_histogram.peak();
    if (peak == 0) {
        return;
    }

    const QPainterPath redPath = histogramPath(m_histogram.red, peak, plotRect);
    const QPainterPath greenPath = histogramPath(m_histogram.green, peak, plotRect);
    const QPainterPath bluePath = histogramPath(m_histogram.blue, peak, plotRect);

    painter.save();
    painter.setClipRect(plotRect);

    // Additive blending reproduces the familiar RGB overlap colors: red+green is
    // yellow, red+blue is magenta, green+blue is cyan, and all three are gray.
    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.fillPath(redPath, QColor(255, 0, 0, 92));
    painter.fillPath(greenPath, QColor(0, 255, 0, 92));
    painter.fillPath(bluePath, QColor(0, 0, 255, 92));

    painter.restore();

    painter.setPen(QPen(QColor(70, 70, 70), 1.0));
    painter.drawRect(plotRect);
}
