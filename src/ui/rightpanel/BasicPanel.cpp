#include "ui/rightpanel/BasicPanel.h"

#include <QEasingCurve>
#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

namespace {

constexpr int kPanelAnimationDurationMs = 280;
constexpr int kTemperatureMinimumKelvin = 2000;
constexpr int kTemperatureMaximumKelvin = 20000;
constexpr int kDefaultTemperatureKelvin = 6500;
constexpr int kTemperatureSliderResolution = 1000000;

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

bool sliderValueFromText(const QString &text,
                         ValueFormat format,
                         int minimum,
                         int maximum,
                         int &value)
{
    bool ok = false;

    if (format == ValueFormat::Exposure) {
        const double stops = text.trimmed().toDouble(&ok);
        const double minimumStops = static_cast<double>(minimum) / 100.0;
        const double maximumStops = static_cast<double>(maximum) / 100.0;
        if (!ok || !std::isfinite(stops)
            || stops < minimumStops || stops > maximumStops) {
            return false;
        }

        value = std::clamp(static_cast<int>(std::lround(stops * 100.0)),
                           minimum, maximum);
        return true;
    }

    const int parsedValue = text.trimmed().toInt(&ok);
    if (!ok || parsedValue < minimum || parsedValue > maximum) {
        return false;
    }

    value = parsedValue;
    return true;
}

int temperatureSliderPositionForKelvin(int kelvin, int minimum, int maximum)
{
    const int clampedKelvin = std::clamp(kelvin, minimum, maximum);
    const double minimumReciprocal = 1.0 / static_cast<double>(minimum);
    const double maximumReciprocal = 1.0 / static_cast<double>(maximum);
    const double kelvinReciprocal = 1.0 / static_cast<double>(clampedKelvin);
    const double normalized = std::clamp(
        (minimumReciprocal - kelvinReciprocal)
            / (minimumReciprocal - maximumReciprocal),
        0.0,
        1.0);

    return static_cast<int>(std::lround(
        normalized * static_cast<double>(kTemperatureSliderResolution)));
}

int temperatureKelvinForSliderPosition(int position, int minimum, int maximum)
{
    const double normalized = std::clamp(
        static_cast<double>(position) / static_cast<double>(kTemperatureSliderResolution),
        0.0,
        1.0);
    const double minimumReciprocal = 1.0 / static_cast<double>(minimum);
    const double maximumReciprocal = 1.0 / static_cast<double>(maximum);
    const double reciprocal = minimumReciprocal
        - normalized * (minimumReciprocal - maximumReciprocal);

    return std::clamp(static_cast<int>(std::lround(1.0 / reciprocal)),
                      minimum,
                      maximum);
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
            : QColor(194, 194, 194);

        constexpr qreal iconScale = 0.95;
        painter.translate(width() / 2.0, (height() / 2.0) - 0.25);
        painter.scale(iconScale, iconScale);
        painter.translate(-12.0, -13.0);

        QPainterPath eye;
        eye.moveTo(12.0, 7.5);
        eye.cubicTo(7.69517, 7.5,
                    4.47617, 11.0833,
                    3.39473, 12.4653);
        eye.cubicTo(3.14595, 12.7832,
                    3.14595, 13.2168,
                    3.39473, 13.5347);
        eye.cubicTo(4.47617, 14.9167,
                    7.69517, 18.5,
                    12.0, 18.5);
        eye.cubicTo(16.3048, 18.5,
                    19.5238, 14.9167,
                    20.6053, 13.5347);
        eye.cubicTo(20.8541, 13.2168,
                    20.8541, 12.7832,
                    20.6053, 12.4653);
        eye.cubicTo(19.5238, 11.0833,
                    16.3048, 7.5,
                    12.0, 7.5);
        eye.closeSubpath();

        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color,
                            1.2 / iconScale,
                            Qt::SolidLine,
                            Qt::RoundCap,
                            Qt::RoundJoin));
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(12.0, 13.0), 2.0, 2.0);
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
                    const int nextHeight = value.toInt();
                    if (height() != nextHeight) {
                        setFixedHeight(nextHeight);
                    }
                    update();
                });

        connect(m_animation, &QVariantAnimation::finished, this,
                [this]() {
                    setFixedHeight(m_expanded ? m_expandedHeight : 0);
                    if (m_content) {
                        m_content->setVisible(m_expanded);
                    }
                    m_animationPixmap = {};
                    update();
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

        m_animation->stop();

        if (expanded) {
            refreshExpandedHeight();
        }

        cacheContentPixmap();
        if (m_content) {
            m_content->hide();
        }

        m_expanded = expanded;
        m_animation->setStartValue(height());
        m_animation->setEndValue(expanded ? m_expandedHeight : 0);
        m_animation->start();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QWidget::paintEvent(event);

        if (!m_animationPixmap.isNull()) {
            QPainter painter(this);
            painter.drawPixmap(0, 0, m_animationPixmap);
        }
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        updateContentGeometry();
    }

private:
    void cacheContentPixmap()
    {
        if (!m_content || !m_animationPixmap.isNull() || m_expandedHeight <= 0) {
            return;
        }

        const bool wasHidden = m_content->isHidden();
        if (wasHidden) {
            m_content->show();
        }

        updateContentGeometry();
        m_content->ensurePolished();

        m_animationPixmap = QPixmap(m_content->size());
        m_animationPixmap.fill(Qt::transparent);
        m_content->render(&m_animationPixmap);

        if (wasHidden) {
            m_content->hide();
        }
    }

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
    QPixmap m_animationPixmap;
    int m_expandedHeight = 0;
    bool m_expanded = true;
};

class BasicSlider final : public QSlider
{
public:
    BasicSlider(bool showNeutralMarker,
                ValueFormat format,
                QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
        , m_showNeutralMarker(showNeutralMarker)
        , m_format(format)
    {
    }

    void setLogicalRange(int minimum, int maximum)
    {
        m_logicalMinimum = minimum;
        m_logicalMaximum = maximum;

        if (m_format == ValueFormat::Temperature) {
            QSlider::setRange(0, kTemperatureSliderResolution);
            return;
        }

        QSlider::setRange(minimum, maximum);
    }

    void setLogicalValue(int value)
    {
        const int clampedValue = std::clamp(value, m_logicalMinimum, m_logicalMaximum);
        if (m_format == ValueFormat::Temperature) {
            QSlider::setValue(temperatureSliderPositionForKelvin(
                clampedValue, m_logicalMinimum, m_logicalMaximum));
            return;
        }

        QSlider::setValue(clampedValue);
    }

    int logicalValue() const
    {
        if (m_format == ValueFormat::Temperature) {
            return temperatureKelvinForSliderPosition(
                QSlider::value(), m_logicalMinimum, m_logicalMaximum);
        }

        return QSlider::value();
    }

    int logicalMinimum() const
    {
        return m_logicalMinimum;
    }

    int logicalMaximum() const
    {
        return m_logicalMaximum;
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
    ValueFormat m_format = ValueFormat::SignedInteger;
    int m_logicalMinimum = 0;
    int m_logicalMaximum = 0;
};

class ResetLabel final : public QLabel
{
public:
    ResetLabel(const QString &text,
               BasicSlider *slider,
               int resetValue,
               QWidget *parent = nullptr)
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
            const bool wasSliderDown = m_slider->isSliderDown();
            if (!wasSliderDown) {
                m_slider->setSliderDown(true);
            }

            m_slider->setLogicalValue(
                overrideValue.isValid() ? overrideValue.toInt() : m_resetValue);

            if (!wasSliderDown) {
                m_slider->setSliderDown(false);
            }

            event->accept();
            return;
        }

        QLabel::mouseDoubleClickEvent(event);
    }

private:
    BasicSlider *m_slider = nullptr;
    int m_resetValue = 0;
};

class EditableValueLabel final : public QLabel
{
public:
    EditableValueLabel(BasicSlider *slider,
                       ValueFormat format,
                       QWidget *parent = nullptr)
        : QLabel(formattedValue(slider->logicalValue(), format), parent)
        , m_slider(slider)
        , m_format(format)
        , m_editor(new QLineEdit(this))
    {
        setObjectName(QStringLiteral("basicValueLabel"));
        setAlignment(Qt::AlignRight | Qt::AlignVCenter);

        m_editor->setFrame(false);
        m_editor->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_editor->setTextMargins(0, 0, 0, 0);
        m_editor->setStyleSheet(QStringLiteral(
            "QLineEdit {"
            "  color: #9f9f9f;"
            "  background: #181818;"
            "  border: 0;"
            "  margin: 0;"
            "  padding: 0;"
            "  font-size: 11px;"
            "  selection-color: #9f9f9f;"
            "  selection-background-color: #181818;"
            "}"));
        m_editor->hide();
        m_editor->installEventFilter(this);

        connect(m_slider, &QSlider::valueChanged, this,
                [this](int) {
                    setText(formattedValue(m_slider->logicalValue(), m_format));
                });
        connect(m_editor, &QLineEdit::editingFinished, this,
                [this]() {
                    if (m_editor->isVisible()) {
                        commitEditing();
                    }
                });
    }

protected:
    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            beginEditing();
            event->accept();
            return;
        }

        QLabel::mousePressEvent(event);
    }

    void resizeEvent(QResizeEvent *event) override
    {
        QLabel::resizeEvent(event);
        m_editor->setGeometry(rect());
    }

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_editor && event->type() == QEvent::KeyPress) {
            const auto *keyEvent = static_cast<QKeyEvent *>(event);
            if (keyEvent->key() == Qt::Key_Escape) {
                cancelEditing();
                return true;
            }
        }

        return QLabel::eventFilter(watched, event);
    }

private:
    void beginEditing()
    {
        m_editor->setGeometry(rect());
        m_editor->setText(formattedValue(m_slider->logicalValue(), m_format));
        m_editor->show();
        m_editor->raise();
        m_editor->setFocus(Qt::MouseFocusReason);
        m_editor->deselect();
        m_editor->setCursorPosition(m_editor->text().size());
    }

    void commitEditing()
    {
        int sliderValue = m_slider->logicalValue();
        if (sliderValueFromText(m_editor->text(), m_format,
                                m_slider->logicalMinimum(), m_slider->logicalMaximum(),
                                sliderValue)) {
            const bool wasSliderDown = m_slider->isSliderDown();
            if (!wasSliderDown) {
                m_slider->setSliderDown(true);
            }

            m_slider->setLogicalValue(sliderValue);

            if (!wasSliderDown) {
                m_slider->setSliderDown(false);
            }
        }

        setText(formattedValue(m_slider->logicalValue(), m_format));
        m_editor->hide();
    }

    void cancelEditing()
    {
        const bool signalsWereBlocked = m_editor->blockSignals(true);
        m_editor->hide();
        m_editor->blockSignals(signalsWereBlocked);
        m_editor->setText(formattedValue(m_slider->logicalValue(), m_format));
    }

    BasicSlider *m_slider = nullptr;
    ValueFormat m_format = ValueFormat::SignedInteger;
    QLineEdit *m_editor = nullptr;
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

    auto *slider = new BasicSlider(showNeutralMarker, format, control);
    slider->setObjectName(sliderObjectName);
    slider->setLogicalRange(minimum, maximum);
    slider->setLogicalValue(value);
    slider->setTracking(true);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *label = new ResetLabel(labelText, slider, value, control);
    label->setObjectName(QStringLiteral("basicControlLabel"));

    auto *valueLabel = new EditableValueLabel(slider, format, control);

    labelRow->addWidget(label);
    labelRow->addStretch(1);
    labelRow->addWidget(valueLabel);

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
    auto *temperatureSlider = addSliderControl(contentLayout,
                                               tr("Temp"),
                                               kTemperatureMinimumKelvin,
                                               kTemperatureMaximumKelvin,
                                               kDefaultTemperatureKelvin,
                                               ValueFormat::Temperature,
                                               false,
                                               QStringLiteral("temperatureSlider"),
                                               content);
    m_temperatureSlider = temperatureSlider;
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

    connect(temperatureSlider, &QSlider::valueChanged, this,
            [this, temperatureSlider](int) {
                if (!m_settingCameraTemperature && !temperatureSlider->isSliderDown()) {
                    emit temperatureChanged(temperatureSlider->logicalValue());
                }
            });
    connect(temperatureSlider, &QSlider::sliderReleased, this,
            [this, temperatureSlider]() {
                if (!m_settingCameraTemperature) {
                    emit temperatureChanged(temperatureSlider->logicalValue());
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

    auto *temperatureSlider = static_cast<BasicSlider *>(m_temperatureSlider);
    const int cameraKelvin = std::clamp(
        kelvin > 0 ? kelvin : kDefaultTemperatureKelvin,
        temperatureSlider->logicalMinimum(),
        temperatureSlider->logicalMaximum());

    temperatureSlider->setProperty("resetValue", cameraKelvin);

    m_settingCameraTemperature = true;
    temperatureSlider->setLogicalValue(cameraKelvin);
    m_settingCameraTemperature = false;
}