#include "ui/histogram/HistogramWidget.h"

#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QPen>
#include <QPolygonF>
#include <QSizePolicy>

#include <algorithm>
#include <array>

namespace {

constexpr int kHistogramPreviewMaximumEdge = 1024;
constexpr int kClippingIndicatorSize = 14;
constexpr int kClippingIndicatorInset = 5;
constexpr int kClippingRange = 1;

qreal normalizedHistogramHeight(quint64 count, quint64 displayPeak)
{
    if (count == 0 || displayPeak == 0) {
        return 0.0;
    }

    return std::min<qreal>(
        static_cast<qreal>(count) / static_cast<qreal>(displayPeak),
        1.0);
}

QPainterPath histogramPath(const std::array<quint64, imaging::HistogramData::kBinCount> &bins,
                           quint64 displayPeak,
                           const QRectF &rect)
{
    QPainterPath path;
    if (displayPeak == 0 || rect.isEmpty()) {
        return path;
    }

    path.moveTo(rect.left(), rect.bottom());

    constexpr qreal lastBin = imaging::HistogramData::kBinCount - 1.0;
    for (int bin = 0; bin < imaging::HistogramData::kBinCount; ++bin) {
        const qreal x = rect.left()
            + (static_cast<qreal>(bin) / lastBin) * rect.width();
        const qreal normalized = normalizedHistogramHeight(
            bins[static_cast<std::size_t>(bin)], displayPeak);
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

quint64 largestInteriorBin(const imaging::HistogramData &histogram)
{
    quint64 peak = 0;
    for (int bin = 1; bin < imaging::HistogramData::kBinCount - 1; ++bin) {
        const std::size_t index = static_cast<std::size_t>(bin);
        peak = std::max({peak,
                         histogram.red[index],
                         histogram.green[index],
                         histogram.blue[index]});
    }
    return peak;
}

quint64 histogramDisplayPeak(const imaging::HistogramData &histogram)
{
    const quint64 peak = histogram.peak();
    if (peak == 0) {
        return 0;
    }

    const quint64 edgePeak = largestEdgeBin(histogram);
    const qreal edgePeakRatio = static_cast<qreal>(edgePeak)
        / static_cast<qreal>(peak);

    // Keep the histogram on a linear scale at all times. If clipping creates
    // an enormous bin at pure black or pure white, use the strongest non-edge
    // tone for vertical normalization. The clipped edge bin is then capped at
    // the top of the graph, preserving the shifted tonal shape while still
    // showing a clear clipping peak at the appropriate edge.
    if (edgePeakRatio >= 0.75) {
        const quint64 interiorPeak = largestInteriorBin(histogram);
        if (interiorPeak > 0) {
            return interiorPeak;
        }
    }

    return peak;
}

quint64 clippedChannelCount(
    const std::array<quint64, imaging::HistogramData::kBinCount> &bins,
    bool highlights)
{
    quint64 count = 0;
    if (highlights) {
        for (int bin = imaging::HistogramData::kBinCount - 1 - kClippingRange;
             bin < imaging::HistogramData::kBinCount;
             ++bin) {
            count += bins[static_cast<std::size_t>(bin)];
        }
    } else {
        for (int bin = 0; bin <= kClippingRange; ++bin) {
            count += bins[static_cast<std::size_t>(bin)];
        }
    }
    return count;
}

QColor clippingIndicatorColor(const imaging::HistogramData &histogram,
                              bool highlights)
{
    const bool red = clippedChannelCount(histogram.red, highlights) > 0;
    const bool green = clippedChannelCount(histogram.green, highlights) > 0;
    const bool blue = clippedChannelCount(histogram.blue, highlights) > 0;

    if (!red && !green && !blue) {
        return QColor(72, 72, 72);
    }

    // Lightroom Classic uses the indicator color to show which RGB channels
    // are clipped. When all three channels clip, the indicator appears white.
    return QColor(red ? 235 : 0,
                  green ? 235 : 0,
                  blue ? 235 : 0);
}

QPolygonF clippingIndicatorPolygon(const QRectF &plotRect, bool highlights)
{
    const qreal top = plotRect.top() + kClippingIndicatorInset;
    if (highlights) {
        const qreal right = plotRect.right() - kClippingIndicatorInset;
        return {
            QPointF(right - kClippingIndicatorSize, top),
            QPointF(right, top),
            QPointF(right, top + kClippingIndicatorSize),
        };
    }

    const qreal left = plotRect.left() + kClippingIndicatorInset;
    return {
        QPointF(left, top),
        QPointF(left + kClippingIndicatorSize, top),
        QPointF(left, top + kClippingIndicatorSize),
    };
}

void drawClippingIndicator(QPainter &painter,
                           const QRectF &plotRect,
                           const imaging::HistogramData &histogram,
                           bool highlights)
{
    const QColor fill = clippingIndicatorColor(histogram, highlights);
    const bool active = fill != QColor(72, 72, 72);

    painter.save();
    painter.setPen(QPen(active ? QColor(245, 245, 245, 170)
                               : QColor(110, 110, 110),
                        1.0));
    painter.setBrush(fill);
    painter.drawPolygon(clippingIndicatorPolygon(plotRect, highlights));
    painter.restore();
}

} // namespace

HistogramWidget::HistogramWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(132);
}

void HistogramWidget::setImage(const QImage &image)
{
    QImage histogramImage = image;
    if (!image.isNull()
        && (image.width() > kHistogramPreviewMaximumEdge
            || image.height() > kHistogramPreviewMaximumEdge)) {
        histogramImage = image.scaled(kHistogramPreviewMaximumEdge,
                                      kHistogramPreviewMaximumEdge,
                                      Qt::KeepAspectRatio,
                                      Qt::FastTransformation);
    }

    m_histogram = imaging::Histogram::calculate(histogramImage);
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

    const quint64 displayPeak = histogramDisplayPeak(m_histogram);
    if (displayPeak == 0) {
        painter.setPen(QPen(QColor(48, 48, 48), 1.0));
        painter.drawRect(frameRect);
        return;
    }

    // Keep bins 0 and 255 just inside the frame so real clipping peaks remain
    // visible as part of the histogram instead of disappearing under the border.
    const QRectF dataRect = plotRect.adjusted(2.0, 0.0, -2.0, 0.0);

    const QPainterPath redPath = histogramPath(
        m_histogram.red, displayPeak, dataRect);
    const QPainterPath greenPath = histogramPath(
        m_histogram.green, displayPeak, dataRect);
    const QPainterPath bluePath = histogramPath(
        m_histogram.blue, displayPeak, dataRect);

    painter.save();
    painter.setClipRect(plotRect);

    painter.setCompositionMode(QPainter::CompositionMode_Plus);
    painter.fillPath(redPath, QColor(255, 55, 55, 78));
    painter.fillPath(greenPath, QColor(55, 255, 95, 78));
    painter.fillPath(bluePath, QColor(70, 115, 255, 78));

    painter.restore();

    painter.setPen(QPen(QColor(48, 48, 48), 1.0));
    painter.drawRect(frameRect);

    drawClippingIndicator(painter, plotRect, m_histogram, false);
    drawClippingIndicator(painter, plotRect, m_histogram, true);
}
