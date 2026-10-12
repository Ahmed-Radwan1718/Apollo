#include "ui/histogram/HistogramWidget.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QSizePolicy>

#include <array>
#include <cmath>

namespace {

qreal normalizedHistogramHeight(quint64 count,
                                quint64 peak,
                                bool logarithmicScale)
{
    if (count == 0 || peak == 0) {
        return 0.0;
    }

    if (logarithmicScale) {
        return std::log1p(static_cast<qreal>(count))
            / std::log1p(static_cast<qreal>(peak));
    }

    return static_cast<qreal>(count) / static_cast<qreal>(peak);
}

QPainterPath histogramPath(const std::array<quint64, imaging::HistogramData::kBinCount> &bins,
                           quint64 peak,
                           const QRectF &rect,
                           bool logarithmicScale)
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
        const qreal normalized = normalizedHistogramHeight(
            bins[static_cast<std::size_t>(bin)], peak, logarithmicScale);
        const qreal y = rect.bottom() - (normalized * rect.height());
        path.lineTo(x, y);
    }

    path.lineTo(rect.right(), rect.bottom());
    path.closeSubpath();
    return path;
}

quint64 largestEdgeBin(const imaging::HistogramData &histogram)
{
    return std::max({histogram.red.front(), histogram.red.back(),
                     histogram.green.front(), histogram.green.back(),
                     histogram.blue.front(), histogram.blue.back()});
}

} // namespace

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setMinimumHeight(132);
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
    return {292, 148};
}

QSize HistogramWidget::minimumSizeHint() const
{
    return {220, 126};
}

void HistogramWidget::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.fillRect(rect(), QColor(24, 24, 24));

    const QRectF plotRect = QRectF(rect()).adjusted(1.0, 3.0, -1.0, -2.0);
    const QRectF frameRect = plotRect.adjusted(0.5, 0.5, -0.5, -0.5);

    if (m_histogram.isEmpty()) {
        painter.setPen(QColor(105, 105, 105));
        painter.drawText(plotRect, Qt::AlignCenter, QStringLiteral("No image"));
        painter.setPen(QPen(QColor(48, 48, 48), 1.0));
        painter.drawRect(frameRect);
        return;
    }

    const quint64 peak = m_histogram.peak();
    if (peak == 0) {
        painter.setPen(QPen(QColor(48, 48, 48), 1.0));
        painter.drawRect(frameRect);
        return;
    }

    // A dominant pure-black or pure-white bin can otherwise flatten every
    // other tone into the baseline. Compress the vertical scale only for that
    // edge-clipping case so the underlying tonal distribution stays readable.
    const qreal edgePeakRatio = static_cast<qreal>(largestEdgeBin(m_histogram))
        / static_cast<qreal>(peak);
    const bool logarithmicScale = edgePeakRatio >= 0.75;

    // Keep bins 0 and 255 just inside the frame so real clipping peaks remain
    // visible as part of the histogram instead of disappearing under the border.
    const QRectF dataRect = plotRect.adjusted(2.0, 0.0, -2.0, 0.0);

    const QPainterPath redPath = histogramPath(
        m_histogram.red, peak, dataRect, logarithmicScale);
    const QPainterPath greenPath = histogramPath(
        m_histogram.green, peak, dataRect, logarithmicScale);
    const QPainterPath bluePath = histogramPath(
        m_histogram.blue, peak, dataRect, logarithmicScale);

    painter.save();
    painter.setClipRect(plotRect);

    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.fillPath(redPath, QColor(255, 55, 55, 78));
    painter.fillPath(greenPath, QColor(55, 255, 95, 78));
    painter.fillPath(bluePath, QColor(70, 115, 255, 78));

    painter.restore();

    painter.setPen(QPen(QColor(48, 48, 48), 1.0));
    painter.drawRect(frameRect);
}
