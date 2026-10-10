#include "ui/rightpanel/BasicPanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QLabel>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

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
                  QWidget *parent)
{
    auto *label = new QLabel(labelText, parent);
    label->setObjectName(QStringLiteral("basicControlLabel"));

    auto *slider = new QSlider(Qt::Horizontal, parent);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setTracking(true);

    layout->addWidget(label, row, 0);
    layout->addWidget(slider, row, 1);
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
    header->setText(tr("BASIC"));
    header->setCheckable(true);
    header->setChecked(true);
    header->setArrowType(Qt::DownArrow);
    header->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    header->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outerLayout->addWidget(header);

    auto *content = new QFrame(this);
    content->setObjectName(QStringLiteral("basicContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(10, 8, 6, 10);
    contentLayout->setSpacing(6);

    contentLayout->addWidget(sectionLabel(tr("White Balance"), content));
    auto *whiteBalanceGrid = new QGridLayout;
    whiteBalanceGrid->setContentsMargins(0, 0, 0, 2);
    whiteBalanceGrid->setHorizontalSpacing(8);
    whiteBalanceGrid->setVerticalSpacing(4);
    whiteBalanceGrid->setColumnStretch(1, 1);
    addSliderRow(whiteBalanceGrid, 0, tr("Temp"), 2000, 50000, 6500, content);
    addSliderRow(whiteBalanceGrid, 1, tr("Tint"), -150, 150, 0, content);
    contentLayout->addLayout(whiteBalanceGrid);

    contentLayout->addWidget(sectionLabel(tr("Tone"), content));
    auto *toneGrid = new QGridLayout;
    toneGrid->setContentsMargins(0, 0, 0, 2);
    toneGrid->setHorizontalSpacing(8);
    toneGrid->setVerticalSpacing(4);
    toneGrid->setColumnStretch(1, 1);
    addSliderRow(toneGrid, 0, tr("Exposure"), -500, 500, 0, content);
    addSliderRow(toneGrid, 1, tr("Contrast"), -100, 100, 0, content);
    addSliderRow(toneGrid, 2, tr("Highlights"), -100, 100, 0, content);
    addSliderRow(toneGrid, 3, tr("Shadows"), -100, 100, 0, content);
    addSliderRow(toneGrid, 4, tr("Whites"), -100, 100, 0, content);
    addSliderRow(toneGrid, 5, tr("Blacks"), -100, 100, 0, content);
    contentLayout->addLayout(toneGrid);

    contentLayout->addWidget(sectionLabel(tr("Presence"), content));
    auto *presenceGrid = new QGridLayout;
    presenceGrid->setContentsMargins(0, 0, 0, 2);
    presenceGrid->setHorizontalSpacing(8);
    presenceGrid->setVerticalSpacing(4);
    presenceGrid->setColumnStretch(1, 1);
    addSliderRow(presenceGrid, 0, tr("Texture"), -100, 100, 0, content);
    addSliderRow(presenceGrid, 1, tr("Clarity"), -100, 100, 0, content);
    addSliderRow(presenceGrid, 2, tr("Dehaze"), -100, 100, 0, content);
    contentLayout->addLayout(presenceGrid);

    contentLayout->addWidget(sectionLabel(tr("Color"), content));
    auto *colorGrid = new QGridLayout;
    colorGrid->setContentsMargins(0, 0, 0, 0);
    colorGrid->setHorizontalSpacing(8);
    colorGrid->setVerticalSpacing(4);
    colorGrid->setColumnStretch(1, 1);
    addSliderRow(colorGrid, 0, tr("Vibrance"), -100, 100, 0, content);
    addSliderRow(colorGrid, 1, tr("Saturation"), -100, 100, 0, content);
    contentLayout->addLayout(colorGrid);

    outerLayout->addWidget(content);

    connect(header, &QToolButton::toggled, this,
            [header, content](bool expanded) {
                header->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
                content->setVisible(expanded);
            });

    setStyleSheet(QStringLiteral(
        "QWidget#basicPanel { background: #202020; }"
        "QToolButton#basicHeader {"
        "  color: #d7d7d7; background: #252525; border: 0;"
        "  border-top: 1px solid #343434; border-bottom: 1px solid #343434;"
        "  text-align: left; font-weight: 600; padding: 7px 6px;"
        "}"
        "QToolButton#basicHeader:hover { background: #2b2b2b; }"
        "QFrame#basicContent { background: #202020; }"
        "QLabel#basicSectionLabel { color: #bdbdbd; font-weight: 600; margin-top: 4px; }"
        "QLabel#basicControlLabel { color: #c8c8c8; min-width: 66px; }"
        "QSlider::groove:horizontal { height: 2px; background: #555555; }"
        "QSlider::sub-page:horizontal { background: #777777; }"
        "QSlider::add-page:horizontal { background: #555555; }"
        "QSlider::handle:horizontal {"
        "  width: 10px; height: 10px; margin: -4px 0;"
        "  border-radius: 5px; background: #d6d6d6;"
        "}"
        "QSlider::handle:horizontal:hover { background: #ffffff; }"));
}
