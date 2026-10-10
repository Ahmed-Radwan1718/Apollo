#include "ui/rightpanel/BasicPanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

enum class ValueFormat {
    Temperature,
    Exposure,
    SignedInteger,
};

QString formattedValue(int value, ValueFormat format)
{
    switch (format) {
    case ValueFormat::Temperature:
        return QString::number(value);
    case ValueFormat::Exposure: {
        const double stops = static_cast<double>(value) / 100.0;
        return QStringLiteral("%1%2")
            .arg(stops > 0.0 ? QStringLiteral("+") : QString(),
                 QString::number(stops, 'f', 2));
    }
    case ValueFormat::SignedInteger:
        return value > 0
            ? QStringLiteral("+%1").arg(value)
            : QString::number(value);
    }

    return {};
}

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("basicSectionLabel"));
    return label;
}

void addSliderRow(QGridLayout *layout,
                  int row,
                  const QString &labelText,
                  int minimum,
                  int maximum,
                  int value,
                  ValueFormat format,
                  QWidget *parent)
{
    auto *label = new QLabel(labelText, parent);
    label->setObjectName(QStringLiteral("basicControlLabel"));

    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setObjectName(QStringLiteral("basicSlider"));
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setTracking(true);

    auto *valueLabel = new QLabel(formattedValue(value, format), parent);
    valueLabel->setObjectName(QStringLiteral("basicValueLabel"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    valueLabel->setMinimumWidth(42);

    QObject::connect(slider, &QSlider::valueChanged, valueLabel,
                     [valueLabel, format](int currentValue) {
                         valueLabel->setText(formattedValue(currentValue, format));
                     });

    layout->addWidget(label, row, 0);
    layout->addWidget(slider, row, 1);
    layout->addWidget(valueLabel, row, 2);
}

} // namespace

BasicPanel::BasicPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("basicPanel"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QToolButton(this);
    header->setObjectName(QStringLiteral("basicHeader"));
    header->setText(tr("Basic"));
    header->setCheckable(true);
    header->setChecked(true);
    header->setArrowType(Qt::DownArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    header->setCursor(Qt::PointingHandCursor);
    header->setFixedHeight(34);
    outerLayout->addWidget(header);

    auto *content = new QFrame(this);
    content->setObjectName(QStringLiteral("basicContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(2, 7, 2, 12);
    contentLayout->setSpacing(5);

    contentLayout->addWidget(sectionLabel(tr("White Balance"), content));
    auto *whiteBalanceGrid = new QGridLayout;
    whiteBalanceGrid->setContentsMargins(0, 0, 0, 4);
    whiteBalanceGrid->setHorizontalSpacing(8);
    whiteBalanceGrid->setVerticalSpacing(5);
    whiteBalanceGrid->setColumnStretch(1, 1);
    addSliderRow(whiteBalanceGrid, 0, tr("Temp"), 2000, 50000, 6500,
                 ValueFormat::Temperature, content);
    addSliderRow(whiteBalanceGrid, 1, tr("Tint"), -150, 150, 0,
                 ValueFormat::SignedInteger, content);
    contentLayout->addLayout(whiteBalanceGrid);

    contentLayout->addWidget(sectionLabel(tr("Tone"), content));
    auto *toneGrid = new QGridLayout;
    toneGrid->setContentsMargins(0, 0, 0, 4);
    toneGrid->setHorizontalSpacing(8);
    toneGrid->setVerticalSpacing(5);
    toneGrid->setColumnStretch(1, 1);
    addSliderRow(toneGrid, 0, tr("Exposure"), -500, 500, 0,
                 ValueFormat::Exposure, content);
    addSliderRow(toneGrid, 1, tr("Contrast"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(toneGrid, 2, tr("Highlights"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(toneGrid, 3, tr("Shadows"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(toneGrid, 4, tr("Whites"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(toneGrid, 5, tr("Blacks"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    contentLayout->addLayout(toneGrid);

    contentLayout->addWidget(sectionLabel(tr("Presence"), content));
    auto *presenceGrid = new QGridLayout;
    presenceGrid->setContentsMargins(0, 0, 0, 4);
    presenceGrid->setHorizontalSpacing(8);
    presenceGrid->setVerticalSpacing(5);
    presenceGrid->setColumnStretch(1, 1);
    addSliderRow(presenceGrid, 0, tr("Texture"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(presenceGrid, 1, tr("Clarity"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(presenceGrid, 2, tr("Dehaze"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    contentLayout->addLayout(presenceGrid);

    contentLayout->addWidget(sectionLabel(tr("Color"), content));
    auto *colorGrid = new QGridLayout;
    colorGrid->setContentsMargins(0, 0, 0, 0);
    colorGrid->setHorizontalSpacing(8);
    colorGrid->setVerticalSpacing(5);
    colorGrid->setColumnStretch(1, 1);
    addSliderRow(colorGrid, 0, tr("Vibrance"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    addSliderRow(colorGrid, 1, tr("Saturation"), -100, 100, 0,
                 ValueFormat::SignedInteger, content);
    contentLayout->addLayout(colorGrid);

    outerLayout->addWidget(content);

    connect(header, &QToolButton::toggled, this,
            [header, content](bool expanded) {
                header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
                content->setVisible(expanded);
            });

    setStyleSheet(QStringLiteral(
        "QWidget#basicPanel {"
        "  background: #181818;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QToolButton#basicHeader {"
        "  color: #d6d6d6;"
        "  background: transparent;"
        "  border: 0;"
        "  border-top: 1px solid #2a2a2a;"
        "  text-align: left;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  padding: 0 6px;"
        "}"
        "QToolButton#basicHeader:hover {"
        "  color: #ffffff;"
        "  background: #1b1b1b;"
        "}"
        "QFrame#basicContent { background: #181818; border: 0; }"
        "QLabel#basicSectionLabel {"
        "  color: #8f8f8f;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding-top: 6px;"
        "  padding-bottom: 2px;"
        "}"
        "QLabel#basicControlLabel {"
        "  color: #c2c2c2;"
        "  font-size: 12px;"
        "  min-width: 64px;"
        "}"
        "QLabel#basicValueLabel {"
        "  color: #8f8f8f;"
        "  font-size: 11px;"
        "}"
        "QSlider#basicSlider { min-height: 14px; }"
        "QSlider#basicSlider::groove:horizontal {"
        "  height: 1px;"
        "  background: #3e3e3e;"
        "}"
        "QSlider#basicSlider::sub-page:horizontal { background: #707070; }"
        "QSlider#basicSlider::add-page:horizontal { background: #3e3e3e; }"
        "QSlider#basicSlider::handle:horizontal {"
        "  width: 8px;"
        "  height: 8px;"
        "  margin: -4px 0;"
        "  border-radius: 4px;"
        "  background: #d7d7d7;"
        "  border: 0;"
        "}"
        "QSlider#basicSlider::handle:horizontal:hover { background: #ffffff; }"));
}
