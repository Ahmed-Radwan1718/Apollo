#include "ui/rightpanel/BasicPanel.h"

#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
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

class BasicTitleButton final : public QToolButton
{
public:
    explicit BasicTitleButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        painter.setPen(underMouse() ? QColor(255, 255, 255) : QColor(214, 214, 214));
        painter.drawText(rect(), Qt::AlignLeft | Qt::AlignVCenter, text());
    }
};

class EyeButton final : public QToolButton
{
public:
    explicit EyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Basic adjustments visibility"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor color = underMouse()
            ? QColor(255, 255, 255)
            : QColor(211, 211, 211);

        const QPointF center(width() / 2.0, height() / 2.0);
        constexpr qreal halfWidth = 7.2;
        constexpr qreal halfHeight = 4.2;

        QPainterPath eye;
        eye.moveTo(center.x() - halfWidth, center.y());
        eye.cubicTo(center.x() - 4.3, center.y() - halfHeight,
                    center.x() + 4.3, center.y() - halfHeight,
                    center.x() + halfWidth, center.y());
        eye.cubicTo(center.x() + 4.3, center.y() + halfHeight,
                    center.x() - 4.3, center.y() + halfHeight,
                    center.x() - halfWidth, center.y());

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(eye);

        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(center, 2.0, 2.0);
    }
};

class ChevronButton final : public QToolButton
{
public:
    explicit ChevronButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(26, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded) {
            return;
        }

        m_expanded = expanded;
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        const QColor color = underMouse()
            ? QColor(255, 255, 255)
            : QColor(190, 190, 190);
        painter.setPen(QPen(color, 1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        const QPointF center(width() / 2.0, height() / 2.0);
        if (m_expanded) {
            painter.drawLine(QPointF(center.x() - 3.5, center.y() - 1.5),
                             QPointF(center.x(), center.y() + 2.0));
            painter.drawLine(QPointF(center.x(), center.y() + 2.0),
                             QPointF(center.x() + 3.5, center.y() - 1.5));
        } else {
            painter.drawLine(QPointF(center.x() - 1.5, center.y() - 3.5),
                             QPointF(center.x() + 2.0, center.y()));
            painter.drawLine(QPointF(center.x() + 2.0, center.y()),
                             QPointF(center.x() - 1.5, center.y() + 3.5));
        }
    }

private:
    bool m_expanded = true;
};

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

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("basicHeader"));
    header->setFixedHeight(40);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *titleButton = new BasicTitleButton(header);
    titleButton->setObjectName(QStringLiteral("basicTitle"));
    titleButton->setText(tr("Basic"));
    titleButton->setCheckable(true);
    titleButton->setChecked(true);
    titleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    titleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    titleButton->setCursor(Qt::PointingHandCursor);
    titleButton->setFocusPolicy(Qt::NoFocus);

    auto *eyeButton = new EyeButton(header);
    auto *chevronButton = new ChevronButton(header);

    headerLayout->addWidget(titleButton, 1);
    headerLayout->addWidget(eyeButton);
    headerLayout->addWidget(chevronButton);
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

    connect(titleButton, &QToolButton::toggled, this,
            [content, chevronButton](bool expanded) {
                content->setVisible(expanded);
                chevronButton->setExpanded(expanded);
            });

    connect(chevronButton, &QToolButton::clicked, titleButton,
            [titleButton]() {
                titleButton->toggle();
            });

    setStyleSheet(QStringLiteral(
        "QWidget#basicPanel {"
        "  background: #181818;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QFrame#basicHeader {"
        "  background: transparent;"
        "  border: 0;"
        "  border-top: 1px solid #2a2a2a;"
        "}"
        "QToolButton#basicTitle {"
        "  color: #d6d6d6;"
        "  background: transparent;"
        "  border: 0;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  padding: 0;"
        "}"
        "QToolButton#basicTitle:hover { color: #ffffff; }"
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
