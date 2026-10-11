#include "ui/rightpanel/BasicPanel.h"

#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QSpinBox>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>

namespace {

constexpr int kPanelAnimationDurationMs = 220;
constexpr int kTemperatureMinimumKelvin = 2000;
constexpr int kTemperatureMaximumKelvin = 50000;
constexpr int kDefaultTemperatureKelvin = 6500;

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
            .arg(stops >= 0.0 ? QStringLiteral("+") : QString(),
                 QString::number(stops, 'f', 2));
    }
    case ValueFormat::SignedInteger:
        return value > 0
            ? QStringLiteral("+%1").arg(value)
            : QString::number(value);
    }

    return {};
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
        , m_animation(new QVariantAnimation(this))
    {
        setFixedSize(26, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);

        m_animation->setDuration(kPanelAnimationDurationMs);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_angle = value.toReal();
                    update();
                });
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded) {
            return;
        }

        m_expanded = expanded;
        m_animation->stop();
        m_animation->setStartValue(m_angle);
        m_animation->setEndValue(expanded ? 90.0 : 0.0);
        m_animation->start();
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
        painter.translate(center);
        painter.rotate(m_angle);

        painter.drawLine(QPointF(-1.5, -3.5), QPointF(2.0, 0.0));
        painter.drawLine(QPointF(2.0, 0.0), QPointF(-1.5, 3.5));
    }

private:
    bool m_expanded = true;
    qreal m_angle = 90.0;
    QVariantAnimation *m_animation = nullptr;
};

class RevealContainer final : public QWidget
{
public:
    explicit RevealContainer(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_animation(new QVariantAnimation(this))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_animation->setDuration(kPanelAnimationDurationMs);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);

        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    setFixedHeight(value.toInt());
                });

        connect(m_animation, &QVariantAnimation::finished, this,
                [this]() {
                    setFixedHeight(m_expanded ? m_expandedHeight : 0);
                });
    }

    void setContentWidget(QWidget *content)
    {
        m_content = content;
        if (!m_content) {
            return;
        }

        m_content->setParent(this);
        m_content->show();
        refreshExpandedHeight();
    }

    void refreshExpandedHeight()
    {
        if (!m_content) {
            return;
        }

        m_content->ensurePolished();
        if (m_content->layout()) {
            m_content->layout()->activate();
            m_expandedHeight = m_content->layout()->sizeHint().height();
        } else {
            m_expandedHeight = m_content->sizeHint().height();
        }

        m_expandedHeight = qMax(0, m_expandedHeight);
        m_content->setFixedHeight(m_expandedHeight);
        updateContentGeometry();

        if (m_animation->state() != QAbstractAnimation::Running) {
            setFixedHeight(m_expanded ? m_expandedHeight : 0);
        }
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded) {
            return;
        }

        if (expanded) {
            refreshExpandedHeight();
        }

        m_expanded = expanded;
        m_animation->stop();
        m_animation->setStartValue(height());
        m_animation->setEndValue(expanded ? m_expandedHeight : 0);
        m_animation->start();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateContentGeometry();
    }

private:
    void updateContentGeometry()
    {
        if (!m_content) {
            return;
        }

        if (m_content->width() != width() || m_content->height() != m_expandedHeight) {
            m_content->setGeometry(0, 0, width(), m_expandedHeight);
        }
    }

    QWidget *m_content = nullptr;
    QVariantAnimation *m_animation = nullptr;
    int m_expandedHeight = 0;
    bool m_expanded = true;
};

class BasicSlider final : public QSlider
{
public:
    BasicSlider(bool showNeutralMarker, QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
        , m_showNeutralMarker(showNeutralMarker)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        if (m_showNeutralMarker) {
            QPainter painter(this);
            painter.setRenderHint(QPainter::Antialiasing, false);
            painter.setPen(QPen(QColor(92, 92, 92), 1.0));

            const qreal x = width() / 2.0;
            const qreal y = height() / 2.0;
            painter.drawLine(QPointF(x, y - 6.0), QPointF(x, y - 5.0));
            painter.drawLine(QPointF(x, y + 5.0), QPointF(x, y + 6.0));
        }

        QSlider::paintEvent(event);
    }

private:
    bool m_showNeutralMarker = false;
};

class ResetLabel final : public QLabel
{
public:
    ResetLabel(const QString &text, QSlider *slider, int resetValue, QWidget *parent = nullptr)
        : QLabel(text, parent)
        , m_slider(slider)
        , m_resetValue(resetValue)
    {
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_slider) {
            const QVariant overrideValue = m_slider->property("resetValue");
            m_slider->setValue(overrideValue.isValid() ? overrideValue.toInt() : m_resetValue);
            event->accept();
            return;
        }

        QLabel::mouseDoubleClickEvent(event);
    }

private:
    QSlider *m_slider = nullptr;
    int m_resetValue = 0;
};

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("basicSectionLabel"));
    return label;
}

BasicSlider *addSliderControl(QVBoxLayout *layout,
                              const QString &labelText,
                              int minimum,
                              int maximum,
                              int value,
                              ValueFormat format,
                              bool showNeutralMarker,
                              const QString &sliderObjectName,
                              QWidget *parent)
{
    auto *control = new QWidget(parent);
    control->setObjectName(QStringLiteral("basicControl"));

    auto *controlLayout = new QVBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(2);

    auto *labelRow = new QHBoxLayout;
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(8);

    auto *slider = new BasicSlider(showNeutralMarker, control);
    slider->setObjectName(sliderObjectName);
    slider->setRange(minimum, maximum);
    slider->setValue(value);
    slider->setTracking(true);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *label = new ResetLabel(labelText, slider, value, control);
    label->setObjectName(QStringLiteral("basicControlLabel"));

    QWidget *valueWidget = nullptr;
    if (format == ValueFormat::Temperature) {
        auto *valueEditor = new QSpinBox(control);
        valueEditor->setObjectName(QStringLiteral("temperatureValueEditor"));
        valueEditor->setRange(minimum, maximum);
        valueEditor->setValue(value);
        valueEditor->setButtonSymbols(QAbstractSpinBox::NoButtons);
        valueEditor->setKeyboardTracking(false);
        valueEditor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        valueEditor->setCursor(Qt::IBeamCursor);
        valueEditor->setFixedWidth(58);

        QObject::connect(slider, &QSlider::valueChanged, valueEditor,
                         [valueEditor](int currentValue) {
                             valueEditor->setValue(currentValue);
                         });
        QObject::connect(valueEditor, &QSpinBox::editingFinished, slider,
                         [valueEditor, slider]() {
                             slider->setValue(valueEditor->value());
                         });

        valueWidget = valueEditor;
    } else {
        auto *valueLabel = new QLabel(formattedValue(value, format), control);
        valueLabel->setObjectName(QStringLiteral("basicValueLabel"));
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        QObject::connect(slider, &QSlider::valueChanged, valueLabel,
                         [valueLabel, format](int currentValue) {
                             valueLabel->setText(formattedValue(currentValue, format));
                         });

        valueWidget = valueLabel;
    }

    labelRow->addWidget(label);
    labelRow->addStretch(1);
    labelRow->addWidget(valueWidget);

    controlLayout->addLayout(labelRow);
    controlLayout->addWidget(slider);
    layout->addWidget(control);
    return slider;
}

void addSectionSpacing(QVBoxLayout *layout)
{
    layout->addSpacing(7);
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

    auto *revealContainer = new RevealContainer(this);
    auto *content = new QFrame(revealContainer);
    content->setObjectName(QStringLiteral("basicContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 16);
    contentLayout->setSpacing(6);

    auto *whiteBalanceLabel = sectionLabel(tr("White Balance"), content);
    whiteBalanceLabel->setStyleSheet(QStringLiteral("color: transparent;"));
    contentLayout->addWidget(whiteBalanceLabel);
    m_temperatureSlider = addSliderControl(contentLayout,
                                           tr("Temp"),
                                           kTemperatureMinimumKelvin,
                                           kTemperatureMaximumKelvin,
                                           kDefaultTemperatureKelvin,
                                           ValueFormat::Temperature,
                                           false,
                                           QStringLiteral("temperatureSlider"),
                                           content);
    addSliderControl(contentLayout, tr("Tint"), -150, 150, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("tintSlider"), content);

    addSectionSpacing(contentLayout);
    auto *toneLabel = sectionLabel(tr("Tone"), content);
    toneLabel->setStyleSheet(QStringLiteral("color: transparent;"));
    contentLayout->addWidget(toneLabel);
    addSliderControl(contentLayout, tr("Exposure"), -500, 500, 0,
                     ValueFormat::Exposure, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Contrast"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Highlights"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Shadows"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Whites"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Blacks"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);

    addSectionSpacing(contentLayout);
    auto *presenceLabel = sectionLabel(tr("Presence"), content);
    presenceLabel->setStyleSheet(QStringLiteral("color: transparent;"));
    contentLayout->addWidget(presenceLabel);
    addSliderControl(contentLayout, tr("Texture"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Clarity"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Dehaze"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);

    addSectionSpacing(contentLayout);
    auto *colorLabel = sectionLabel(tr("Color"), content);
    colorLabel->setStyleSheet(QStringLiteral("color: transparent;"));
    contentLayout->addWidget(colorLabel);
    addSliderControl(contentLayout, tr("Vibrance"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);
    addSliderControl(contentLayout, tr("Saturation"), -100, 100, 0,
                     ValueFormat::SignedInteger, true,
                     QStringLiteral("basicSlider"), content);

    revealContainer->setContentWidget(content);
    outerLayout->addWidget(revealContainer);

    connect(titleButton, &QToolButton::toggled, this,
            [revealContainer, chevronButton](bool expanded) {
                chevronButton->setExpanded(expanded);
                revealContainer->setExpanded(expanded);
            });

    connect(chevronButton, &QToolButton::clicked, titleButton,
            [titleButton]() {
                titleButton->toggle();
            });

    connect(m_temperatureSlider, &QSlider::valueChanged, this,
            [this](int kelvin) {
                if (!m_settingCameraTemperature && !m_temperatureSlider->isSliderDown()) {
                    emit temperatureChanged(kelvin);
                }
            });
    connect(m_temperatureSlider, &QSlider::sliderReleased, this,
            [this]() {
                if (!m_settingCameraTemperature) {
                    emit temperatureChanged(m_temperatureSlider->value());
                }
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
        "QFrame#basicContent, QWidget#basicControl { background: #181818; border: 0; }"
        "QLabel#basicSectionLabel {"
        "  color: #b3b3b3;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding-top: 1px;"
        "  padding-bottom: 1px;"
        "}"
        "QLabel#basicControlLabel {"
        "  color: #d0d0d0;"
        "  font-size: 12px;"
        "}"
        "QLabel#basicValueLabel {"
        "  color: #9f9f9f;"
        "  font-size: 11px;"
        "}"
        "QSpinBox#temperatureValueEditor {"
        "  color: #9f9f9f;"
        "  background: transparent;"
        "  border: 1px solid transparent;"
        "  border-radius: 2px;"
        "  padding: 0 2px;"
        "  font-size: 11px;"
        "}"
        "QSpinBox#temperatureValueEditor:hover {"
        "  color: #d8d8d8;"
        "  background: #202020;"
        "}"
        "QSpinBox#temperatureValueEditor:focus {"
        "  color: #ffffff;"
        "  background: #242424;"
        "  border: 1px solid #555555;"
        "}"
        "QSlider#basicSlider, QSlider#temperatureSlider, QSlider#tintSlider {"
        "  min-height: 14px;"
        "  max-height: 14px;"
        "}"
        "QSlider#basicSlider::groove:horizontal {"
        "  height: 2px;"
        "  border-radius: 1px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #303030, stop:0.5 #626262, stop:1 #9b9b9b);"
        "}"
        "QSlider#basicSlider::sub-page:horizontal,"
        "QSlider#basicSlider::add-page:horizontal { background: transparent; }"
        "QSlider#temperatureSlider::groove:horizontal {"
        "  height: 2px;"
        "  border-radius: 1px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #46658f, stop:0.5 #696969, stop:1 #cbb75a);"
        "}"
        "QSlider#temperatureSlider::sub-page:horizontal,"
        "QSlider#temperatureSlider::add-page:horizontal { background: transparent; }"
        "QSlider#tintSlider::groove:horizontal {"
        "  height: 2px;"
        "  border-radius: 1px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #38b96b, stop:0.5 #696969, stop:1 #c94fb3);"
        "}"
        "QSlider#tintSlider::sub-page:horizontal,"
        "QSlider#tintSlider::add-page:horizontal { background: transparent; }"
        "QSlider#basicSlider::handle:horizontal,"
        "QSlider#temperatureSlider::handle:horizontal,"
        "QSlider#tintSlider::handle:horizontal {"
        "  width: 10px;"
        "  height: 10px;"
        "  margin: -4px 0;"
        "  border-radius: 5px;"
        "  background: #d9d9d9;"
        "  border: 0;"
        "}"
        "QSlider#basicSlider::handle:horizontal:hover,"
        "QSlider#temperatureSlider::handle:horizontal:hover,"
        "QSlider#tintSlider::handle:horizontal:hover { background: #ffffff; }"));

    revealContainer->refreshExpandedHeight();
}

void BasicPanel::setCameraTemperatureKelvin(int kelvin)
{
    if (!m_temperatureSlider) {
        return;
    }

    const int cameraKelvin = std::clamp(
        kelvin > 0 ? kelvin : kDefaultTemperatureKelvin,
        m_temperatureSlider->minimum(),
        m_temperatureSlider->maximum());

    m_temperatureSlider->setProperty("resetValue", cameraKelvin);

    m_settingCameraTemperature = true;
    m_temperatureSlider->setValue(cameraKelvin);
    m_settingCameraTemperature = false;
}
