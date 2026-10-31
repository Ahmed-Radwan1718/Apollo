#include "ui/rightpanel/ColorMixerPanel.h"

#include "app/MainWindow.h"
#include "ui/rightpanel/WheelIgnoringSlider.h"

#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <array>

namespace {

constexpr std::array<const char *, imaging::kColorMixerBandCount> kBandNames = {
    "Red", "Orange", "Yellow", "Green", "Aqua", "Blue", "Purple", "Magenta"
};

QString signedValue(int value)
{
    return value > 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
}

QString modeName(int mode)
{
    switch (mode) {
    case 0:
        return QObject::tr("Hue");
    case 1:
        return QObject::tr("Saturation");
    default:
        return QObject::tr("Luminance");
    }
}

class ColorMixerTitleButton final : public QToolButton
{
public:
    explicit ColorMixerTitleButton(QWidget *parent = nullptr)
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

class ColorMixerEyeButton final : public QToolButton
{
public:
    explicit ColorMixerEyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setCheckable(true);
        setChecked(true);
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Color Mixer visibility"));
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QColor color = underMouse() ? QColor(255, 255, 255) : QColor(194, 194, 194);
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));

        QPainterPath eye;
        eye.moveTo(5.0, 15.0);
        eye.cubicTo(9.0, 8.5, 19.0, 8.5, 23.0, 15.0);
        eye.cubicTo(19.0, 21.5, 9.0, 21.5, 5.0, 15.0);
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(14.0, 15.0), 2.0, 2.0);

        if (!isChecked()) {
            painter.drawLine(QPointF(6.0, 22.0), QPointF(22.0, 8.0));
        }
    }
};

class ResetLabel final : public QLabel
{
public:
    ResetLabel(const QString &text, QSlider *slider, QWidget *parent = nullptr)
        : QLabel(text, parent)
        , m_slider(slider)
    {
    }

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_slider) {
            m_slider->setValue(0);
            event->accept();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }

private:
    QSlider *m_slider = nullptr;
};

} // namespace

ColorMixerPanel *ColorMixerPanel::install(MainWindow *window)
{
    if (!window) {
        return nullptr;
    }

    QWidget *container = window->findChild<QWidget *>(QStringLiteral("adjustmentsContainer"));
    auto *layout = container ? qobject_cast<QVBoxLayout *>(container->layout()) : nullptr;
    if (!container || !layout) {
        return nullptr;
    }

    if (QWidget *existing = container->findChild<QWidget *>(
            QStringLiteral("colorMixerPanel"), Qt::FindDirectChildrenOnly)) {
        auto *panel = static_cast<ColorMixerPanel *>(existing);
        window->m_colorMixerPanel = panel;
        return panel;
    }

    auto *panel = new ColorMixerPanel(window, container);
    QWidget *basicPanel = container->findChild<QWidget *>(
        QStringLiteral("basicPanel"), Qt::FindDirectChildrenOnly);
    const int basicIndex = basicPanel ? layout->indexOf(basicPanel) : -1;
    layout->insertWidget(basicIndex >= 0 ? basicIndex + 1 : 0, panel);
    window->m_colorMixerPanel = panel;
    panel->setSettings(window->m_requestedState.colorMixer);
    return panel;
}

ColorMixerPanel::ColorMixerPanel(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    setObjectName(QStringLiteral("colorMixerPanel"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("colorMixerHeader"));
    header->setFixedHeight(40);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *titleButton = new ColorMixerTitleButton(header);
    titleButton->setObjectName(QStringLiteral("colorMixerTitle"));
    titleButton->setText(tr("Color Mixer"));
    titleButton->setCheckable(true);
    titleButton->setChecked(false);
    titleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    titleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    titleButton->setCursor(Qt::PointingHandCursor);
    titleButton->setFocusPolicy(Qt::NoFocus);

    m_eyeButton = new ColorMixerEyeButton(header);
    auto *chevron = new QToolButton(header);
    chevron->setObjectName(QStringLiteral("colorMixerChevron"));
    chevron->setText(QStringLiteral("›"));
    chevron->setFixedSize(26, 30);
    chevron->setCursor(Qt::PointingHandCursor);
    chevron->setFocusPolicy(Qt::NoFocus);

    headerLayout->addWidget(titleButton, 1);
    headerLayout->addWidget(m_eyeButton);
    headerLayout->addWidget(chevron);
    outerLayout->addWidget(header);

    m_content = new QFrame(this);
    m_content->setObjectName(QStringLiteral("colorMixerContent"));
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(12, 10, 12, 16);
    contentLayout->setSpacing(7);

    auto *tabs = new QHBoxLayout;
    tabs->setContentsMargins(0, 0, 0, 5);
    tabs->setSpacing(4);
    const std::array<QString, 3> modeLabels = {tr("Hue"), tr("Saturation"), tr("Luminance")};
    for (std::size_t index = 0; index < m_modeButtons.size(); ++index) {
        auto *button = new QToolButton(m_content);
        button->setObjectName(QStringLiteral("colorMixerModeButton"));
        button->setText(modeLabels[index]);
        button->setCheckable(true);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_modeButtons[index] = button;
        tabs->addWidget(button);
        connect(button, &QToolButton::clicked, this,
                [this, index]() { setMode(static_cast<Mode>(index)); });
    }
    contentLayout->addLayout(tabs);

    for (std::size_t index = 0; index < imaging::kColorMixerBandCount; ++index) {
        auto *control = new QWidget(m_content);
        control->setObjectName(QStringLiteral("colorMixerControl"));
        auto *controlLayout = new QVBoxLayout(control);
        controlLayout->setContentsMargins(0, 0, 0, 0);
        controlLayout->setSpacing(2);

        auto *labelRow = new QHBoxLayout;
        labelRow->setContentsMargins(0, 0, 0, 0);
        labelRow->setSpacing(8);

        auto *slider = new WheelIgnoringSlider(Qt::Horizontal, control);
        slider->setObjectName(QStringLiteral("colorMixerSlider"));
        slider->setRange(-100, 100);
        slider->setValue(0);
        slider->setTracking(true);
        slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        slider->setProperty("bandIndex", static_cast<int>(index));
        m_sliders[index] = slider;

        auto *label = new ResetLabel(tr(kBandNames[index]), slider, control);
        label->setObjectName(QStringLiteral("colorMixerControlLabel"));
        auto *valueLabel = new QLabel(QStringLiteral("0"), control);
        valueLabel->setObjectName(QStringLiteral("colorMixerValueLabel"));
        valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        m_valueLabels[index] = valueLabel;

        labelRow->addWidget(label);
        labelRow->addStretch(1);
        labelRow->addWidget(valueLabel);
        controlLayout->addLayout(labelRow);
        controlLayout->addWidget(slider);
        contentLayout->addWidget(control);

        connect(slider, &QSlider::valueChanged, this,
                [this, slider, valueLabel](int) {
                    handleSliderValueChanged(slider, valueLabel);
                });
        connect(slider, &QSlider::sliderReleased, this,
                [this, slider]() { handleSliderReleased(slider); });
    }

    outerLayout->addWidget(m_content);
    m_content->hide();

    connect(titleButton, &QToolButton::toggled, this,
            [this, chevron](bool expanded) {
                m_content->setVisible(expanded);
                chevron->setText(expanded ? QStringLiteral("⌄") : QStringLiteral("›"));
            });
    connect(chevron, &QToolButton::clicked, titleButton,
            [titleButton]() { titleButton->toggle(); });
    connect(m_eyeButton, &QToolButton::toggled, this,
            [this](bool enabled) { handleEyeToggled(enabled); });

    setStyleSheet(QStringLiteral(
        "QWidget#colorMixerPanel { background: #181818; font-family: \"Segoe UI Variable\"; }"
        "QFrame#colorMixerHeader { background: transparent; border: 0; border-top: 1px solid #2a2a2a; }"
        "QToolButton#colorMixerTitle { color: #d6d6d6; background: transparent; border: 0; font-size: 13px; font-weight: 600; padding: 0; }"
        "QToolButton#colorMixerTitle:hover { color: #ffffff; }"
        "QToolButton#colorMixerChevron { color: #bebebe; background: transparent; border: 0; font-size: 17px; }"
        "QToolButton#colorMixerChevron:hover { color: #ffffff; }"
        "QFrame#colorMixerContent, QWidget#colorMixerControl { background: #181818; border: 0; }"
        "QToolButton#colorMixerModeButton { color: #9f9f9f; background: transparent; border: 0; border-bottom: 2px solid transparent; padding: 4px 2px 5px 2px; font-size: 11px; }"
        "QToolButton#colorMixerModeButton:checked { color: #f1f1f1; border-bottom-color: #c8c8c8; }"
        "QToolButton#colorMixerModeButton:hover { color: #ffffff; }"
        "QLabel#colorMixerControlLabel { color: #d0d0d0; font-size: 12px; }"
        "QLabel#colorMixerValueLabel { color: #9f9f9f; font-size: 11px; }"
        "QSlider#colorMixerSlider { min-height: 14px; max-height: 14px; }"
        "QSlider#colorMixerSlider::groove:horizontal { height: 2px; border-radius: 1px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #454545, stop:0.5 #777777, stop:1 #a2a2a2); }"
        "QSlider#colorMixerSlider::sub-page:horizontal, QSlider#colorMixerSlider::add-page:horizontal { background: transparent; }"
        "QSlider#colorMixerSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; background: #d9d9d9; border: 0; }"
        "QSlider#colorMixerSlider::handle:horizontal:hover { background: #ffffff; }"
        "QSlider#colorMixerSlider:disabled::handle:horizontal { background: #6f6f6f; }"));

    setMode(Mode::Hue);
}

void ColorMixerPanel::setSettings(const imaging::ColorMixerSettings &settings)
{
    m_settingSettings = true;
    m_settings = settings;
    for (auto *values : {&m_settings.hue, &m_settings.saturation, &m_settings.luminance}) {
        for (int &value : *values) {
            value = std::clamp(value, -100, 100);
        }
    }
    if (m_eyeButton) {
        m_eyeButton->setChecked(m_settings.enabled);
    }
    refreshSliders();
    refreshEnabledState();
    m_settingSettings = false;
}

imaging::ColorMixerSettings ColorMixerPanel::settings() const
{
    return m_settings;
}

bool ColorMixerPanel::isEditing() const
{
    return std::any_of(m_sliders.cbegin(), m_sliders.cend(),
                       [](const QSlider *slider) {
                           return slider && slider->isSliderDown();
                       });
}

void ColorMixerPanel::setMode(Mode mode)
{
    m_mode = mode;
    for (std::size_t index = 0; index < m_modeButtons.size(); ++index) {
        if (m_modeButtons[index]) {
            m_modeButtons[index]->setChecked(index == static_cast<std::size_t>(mode));
        }
    }
    refreshSliders();
}

void ColorMixerPanel::refreshSliders()
{
    const bool previous = m_settingSettings;
    m_settingSettings = true;
    const auto &values = valuesForMode(m_mode);
    for (std::size_t index = 0; index < m_sliders.size(); ++index) {
        if (m_sliders[index]) {
            m_sliders[index]->setValue(values[index]);
        }
        if (m_valueLabels[index]) {
            m_valueLabels[index]->setText(signedValue(values[index]));
        }
    }
    m_settingSettings = previous;
}

void ColorMixerPanel::refreshEnabledState()
{
    if (m_content) {
        m_content->setEnabled(m_settings.enabled);
    }
    if (m_eyeButton) {
        m_eyeButton->update();
    }
}

void ColorMixerPanel::handleSliderValueChanged(QSlider *slider, QLabel *valueLabel)
{
    if (!slider) {
        return;
    }

    if (valueLabel) {
        valueLabel->setText(signedValue(slider->value()));
    }
    if (m_settingSettings) {
        return;
    }

    const int bandIndex = slider->property("bandIndex").toInt();
    if (bandIndex < 0 || bandIndex >= static_cast<int>(imaging::kColorMixerBandCount)) {
        return;
    }
    valuesForMode(m_mode)[static_cast<std::size_t>(bandIndex)] = slider->value();

    if (!m_window || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.colorMixer = m_settings;
    if (slider->isSliderDown()) {
        m_window->requestPreview(state);
    } else {
        m_window->requestFullRender(state, historyLabelForSlider(slider));
    }
}

void ColorMixerPanel::handleSliderReleased(QSlider *slider)
{
    if (m_settingSettings || !m_window || !slider
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.colorMixer = m_settings;
    m_window->requestFullRender(state, historyLabelForSlider(slider));
}

void ColorMixerPanel::handleEyeToggled(bool enabled)
{
    if (m_settingSettings) {
        return;
    }
    m_settings.enabled = enabled;
    refreshEnabledState();

    if (!m_window || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.colorMixer = m_settings;
    m_window->requestFullRender(state,
                                enabled ? tr("Color Mixer On") : tr("Color Mixer Off"));
}

std::array<int, imaging::kColorMixerBandCount> &ColorMixerPanel::valuesForMode(Mode mode)
{
    switch (mode) {
    case Mode::Hue:
        return m_settings.hue;
    case Mode::Saturation:
        return m_settings.saturation;
    case Mode::Luminance:
        return m_settings.luminance;
    }
    return m_settings.hue;
}

const std::array<int, imaging::kColorMixerBandCount> &ColorMixerPanel::valuesForMode(Mode mode) const
{
    switch (mode) {
    case Mode::Hue:
        return m_settings.hue;
    case Mode::Saturation:
        return m_settings.saturation;
    case Mode::Luminance:
        return m_settings.luminance;
    }
    return m_settings.hue;
}

QString ColorMixerPanel::historyLabelForSlider(const QSlider *slider) const
{
    if (!slider) {
        return tr("Color Mixer");
    }

    const int bandIndex = slider->property("bandIndex").toInt();
    const QString band = bandIndex >= 0
            && bandIndex < static_cast<int>(imaging::kColorMixerBandCount)
        ? tr(kBandNames[static_cast<std::size_t>(bandIndex)])
        : tr("Color");
    return QStringLiteral("%1 %2 %3")
        .arg(band,
             modeName(static_cast<int>(m_mode)),
             signedValue(slider->value()));
}
