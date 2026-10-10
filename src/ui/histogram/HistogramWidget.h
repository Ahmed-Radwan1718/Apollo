#pragma once

#include "imaging/Histogram.h"

#include <QWidget>

class QImage;
class QPaintEvent;

class HistogramWidget final : public QWidget
{
public:
    explicit HistogramWidget(QWidget *parent = nullptr);

    void setImage(const QImage &image);
    void clear();

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    imaging::HistogramData m_histogram;
};
