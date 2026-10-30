#include "ui/rightpanel/EffectsPanel.h"

#include "app/MainWindow.h"
#include "imaging/EffectsProcessor.h"
#include "imaging/TemperatureRenderService.h"
#include "ui/canvas/PhotoCanvas.h"
#include "ui/histogram/HistogramWidget.h"
#include "ui/leftpanel/NavigatorPanel.h"
#include "ui/rightpanel/WheelIgnoringSlider.h"

#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QSlider>
#include <QSizePolicy>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>

namespace {

constexpr int kPanelAnimationDurationMs = 360;
constexpr int kPanelAnimationMinimumDurationMs = 90;

class EffectsTitleButton final : public QToolButton
{
public:
    explicit EffectsTitleButton(QWidget *parent = nullptr)
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

class EffectsEyeButton final : public QToolButton
{
public:
    explicit EffectsEyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Effects adjustments visibility"));
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
        painter.setBrush(Qt::NoBrush);
        painter.setPen(QPen(color, 1.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QRectF eyeRect(5.0, 8.0, 18.0, 12.0);
        QPainterPath eye;
        eye.moveTo(eyeRect.left(), eyeRect.center().y());
        eye.cubicTo(9.0, 5.5, 19.0, 5.5, eyeRect.right(), eyeRect.center().y());
        eye.cubicTo(19.0, 22.5, 9.0, 22.5, eyeRect.left(), eyeRect.center().y());
        painter.drawPath(eye);
        painter.drawEllipse(QPointF(14.0, 14.0), 2.0, 2.0);
    }
};

class EffectsChevronButton final : public QToolButton
{
public:
    explicit EffectsChevronButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(26, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setExpansionProgress(qreal progress)
    {
        m_progress = std::clamp(progress, 0.0, 1.0);
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
        painter.translate(center);
        painter.rotate(90.0 * m_progress);
        painter.drawLine(QPointF(-1.5, -3.5), QPointF(2.0, 0.0));
        painter.drawLine(QPointF(2.0, 0.0), QPointF(-1.5, 3.5));
    }

private:
    qreal m_progress = 0.0;
};

class EffectsRevealContainer final : public QWidget
{
public:
    explicit EffectsRevealContainer(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_animation(new QVariantAnimation(this))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        m_animation->setEasingCurve(QEasingCurve::InOutSine);
        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    const qreal progress = value.toReal();
                    setFixedHeight(qRound(progress * m_expandedHeight));
                    if (m_content) {
                        m_content->setGeometry(0, 0, width(), m_expandedHeight);
                        m_content->setVisible(progress > 0.0);
                    }
                    if (m_progressCallback) {
                        m_progressCallback(progress);
                    }
                });
        connect(m_animation, &QVariantAnimation::finished, this,
                [this]() {
                    if (m_content) {
                        m_content->setVisible(m_expanded);
                    }
                });
    }

    void setContentWidget(QWidget *content)
    {
        m_content = content;
        if (!m_content) {
            return;
        }
        m_content->setParent(this);
        refreshExpandedHeight();
        m_content->hide();
        setFixedHeight(0);
    }

    void setProgressCallback(std::function<void(qreal)> callback)
    {
        m_progressCallback = std::move(callback);
        if (m_progressCallback) {
            m_progressCallback(m_expanded ? 1.0 : 0.0);
        }
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
        m_expandedHeight = std::max(0, m_expandedHeight);
        m_content->setFixedHeight(m_expandedHeight);
        m_content->setGeometry(0, 0, width(), m_expandedHeight);
    }

    void setExpanded(bool expanded)
    {
        refreshExpandedHeight();
        const qreal currentProgress = m_expandedHeight > 0
            ? std::clamp(static_cast<qreal>(height())
                             / static_cast<qreal>(m_expandedHeight),
                         0.0,
                         1.0)
            : 0.0;
        const qreal targetProgress = expanded ? 1.0 : 0.0;
        const qreal distance = std::abs(targetProgress - currentProgress);
        const int duration = std::max(
            kPanelAnimationMinimumDurationMs,
            static_cast<int>(std::lround(kPanelAnimationDurationMs * distance)));

        m_expanded = expanded;
        m_animation->stop();
        if (m_content && expanded) {
            m_content->show();
        }
        m_animation->setDuration(duration);
        m_animation->setStartValue(currentProgress);
        m_animation->setEndValue(targetProgress);
        m_animation->start();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (m_content) {
            m_content->setGeometry(0, 0, width(), m_expandedHeight);
        }
    }

private:
    QWidget *m_content = nullptr;
    QVariantAnimation *m_animation = nullptr;
    std::function<void(qreal)> m_progressCallback;
    int m_expandedHeight = 0;
    bool m_expanded = false;
};

class EffectsResetLabel final : public QLabel
{
public:
    EffectsResetLabel(const QString &text,
                      QSlider *slider,
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
            const bool wasSliderDown = m_slider->isSliderDown();
            if (!wasSliderDown) {
                m_slider->setSliderDown(true);
            }
            m_slider->setValue(m_resetValue);
            if (!wasSliderDown) {
                m_slider->setSliderDown(false);
            }
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
    label->setObjectName(QStringLiteral("effectsSectionLabel"));
    return label;
}

} // namespace

EffectsPanel *EffectsPanel::install(MainWindow *window)
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
            QStringLiteral("effectsPanel"), Qt::FindDirectChildrenOnly)) {
        return static_cast<EffectsPanel *>(existing);
    }

    auto *panel = new EffectsPanel(window, container);
    layout->insertWidget(std::max(0, layout->count() - 1), panel);
    return panel;
}

EffectsPanel::EffectsPanel(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    setObjectName(QStringLiteral("effectsPanel"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("effectsHeader"));
    header->setFixedHeight(40);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *titleButton = new EffectsTitleButton(header);
    titleButton->setObjectName(QStringLiteral("effectsTitle"));
    titleButton->setText(tr("Effects"));
    titleButton->setCheckable(true);
    titleButton->setChecked(false);
    titleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    titleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    titleButton->setCursor(Qt::PointingHandCursor);
    titleButton->setFocusPolicy(Qt::NoFocus);

    auto *eyeButton = new EffectsEyeButton(header);
    auto *chevronButton = new EffectsChevronButton(header);

    headerLayout->addWidget(titleButton, 1);
    headerLayout->addWidget(eyeButton);
    headerLayout->addWidget(chevronButton);
    outerLayout->addWidget(header);

    auto *revealContainer = new EffectsRevealContainer(this);
    auto *content = new QFrame(revealContainer);
    content->setObjectName(QStringLiteral("effectsContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 16);
    contentLayout->setSpacing(6);

    contentLayout->addWidget(sectionLabel(tr("Vignette"), content));
    m_vignetteAmountSlider = addSlider(content, contentLayout,
                                       tr("Amount"), tr("Vignette Amount"),
                                       -100, 100, 0, true);
    m_vignetteMidpointSlider = addSlider(content, contentLayout,
                                         tr("Midpoint"), tr("Vignette Midpoint"),
                                         0, 100, 50);
    m_vignetteRoundnessSlider = addSlider(content, contentLayout,
                                          tr("Roundness"), tr("Vignette Roundness"),
                                          -100, 100, 0, true);
    m_vignetteFeatherSlider = addSlider(content, contentLayout,
                                        tr("Feather"), tr("Vignette Feather"),
                                        0, 100, 50);
    m_vignetteHighlightsSlider = addSlider(content, contentLayout,
                                           tr("Highlights"), tr("Vignette Highlights"),
                                           0, 100, 0);

    contentLayout->addSpacing(7);
    contentLayout->addWidget(sectionLabel(tr("Grain"), content));
    m_grainAmountSlider = addSlider(content, contentLayout,
                                    tr("Amount"), tr("Grain Amount"),
                                    0, 100, 0);
    m_grainSizeSlider = addSlider(content, contentLayout,
                                  tr("Size"), tr("Grain Size"),
                                  0, 100, 25);
    m_grainRoughnessSlider = addSlider(content, contentLayout,
                                       tr("Roughness"), tr("Grain Roughness"),
                                       0, 100, 50);

    revealContainer->setContentWidget(content);
    revealContainer->setProgressCallback(
        [chevronButton](qreal progress) {
            chevronButton->setExpansionProgress(progress);
        });
    outerLayout->addWidget(revealContainer);

    connect(titleButton, &QToolButton::toggled, this,
            [revealContainer](bool expanded) {
                revealContainer->setExpanded(expanded);
            });
    connect(chevronButton, &QToolButton::clicked, titleButton,
            [titleButton]() {
                titleButton->toggle();
            });

    setStyleSheet(QStringLiteral(
        "QWidget#effectsPanel { background: #181818; font-family: \"Segoe UI Variable\"; }"
        "QFrame#effectsHeader { background: transparent; border: 0; border-top: 1px solid #2a2a2a; }"
        "QToolButton#effectsTitle { color: #d6d6d6; background: transparent; border: 0; font-size: 13px; font-weight: 600; padding: 0; }"
        "QToolButton#effectsTitle:hover { color: #ffffff; }"
        "QFrame#effectsContent, QWidget#effectsControl { background: #181818; border: 0; }"
        "QLabel#effectsSectionLabel { color: #b3b3b3; font-size: 11px; font-weight: 600; padding-top: 1px; padding-bottom: 1px; }"
        "QLabel#effectsControlLabel { color: #d0d0d0; font-size: 12px; }"
        "QLabel#effectsValueLabel { color: #9f9f9f; font-size: 11px; }"
        "QSlider#effectsSlider { min-height: 14px; max-height: 14px; }"
        "QSlider#effectsSlider::groove:horizontal { height: 2px; border-radius: 1px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #303030, stop:0.5 #626262, stop:1 #9b9b9b); }"
        "QSlider#effectsSlider::sub-page:horizontal, QSlider#effectsSlider::add-page:horizontal { background: transparent; }"
        "QSlider#effectsSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; background: #d9d9d9; border: 0; }"
        "QSlider#effectsSlider::handle:horizontal:hover { background: #ffffff; }"));

    if (m_window) {
        if (auto *historyList = m_window->findChild<QListWidget *>(
                QStringLiteral("historyList"))) {
            connect(historyList, &QListWidget::currentRowChanged, this,
                    [this](int) {
                        syncFromWindowState();
                    });
        }

        if (m_window->m_renderService) {
            connect(m_window->m_renderService,
                    &imaging::TemperatureRenderService::renderReady,
                    this,
                    [this](const QString &filePath,
                           int temperatureKelvin,
                           int tint,
                           const imaging::RawCalibration &calibration,
                           int exposureHundredths,
                           int highlights,
                           int shadows,
                           int whites,
                           int blacks,
                           int contrast,
                           int texture,
                           int clarity,
                           int dehaze,
                           int saturation,
                           int vibrance,
                           bool preview,
                           const QImage &image,
                           const QString &) {
                        DevelopState renderedState;
                        renderedState.temperatureKelvin = temperatureKelvin;
                        renderedState.tint = tint;
                        renderedState.exposureHundredths = exposureHundredths;
                        renderedState.highlights = highlights;
                        renderedState.shadows = shadows;
                        renderedState.whites = whites;
                        renderedState.blacks = blacks;
                        renderedState.contrast = contrast;
                        renderedState.texture = texture;
                        renderedState.clarity = clarity;
                        renderedState.dehaze = dehaze;
                        renderedState.saturation = saturation;
                        renderedState.vibrance = vibrance;
                        renderedState.calibration = calibration;
                        applyRenderedEffects(filePath, renderedState, preview, image);
                    });
        }
    }

    syncFromWindowState();
}

QSlider *EffectsPanel::addSlider(QFrame *content,
                                 QVBoxLayout *layout,
                                 const QString &labelText,
                                 const QString &historyName,
                                 int minimum,
                                 int maximum,
                                 int defaultValue,
                                 bool signedValue)
{
    auto *control = new QWidget(content);
    control->setObjectName(QStringLiteral("effectsControl"));
    auto *controlLayout = new QVBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(2);

    auto *labelRow = new QHBoxLayout;
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(8);

    auto *slider = new WheelIgnoringSlider(Qt::Horizontal, control);
    slider->setObjectName(QStringLiteral("effectsSlider"));
    slider->setRange(minimum, maximum);
    slider->setValue(defaultValue);
    slider->setTracking(true);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    slider->setProperty("historyName", historyName);
    slider->setProperty("signedValue", signedValue);

    auto *label = new EffectsResetLabel(labelText, slider, defaultValue, control);
    label->setObjectName(QStringLiteral("effectsControlLabel"));

    auto *valueLabel = new QLabel(control);
    valueLabel->setObjectName(QStringLiteral("effectsValueLabel"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    labelRow->addWidget(label);
    labelRow->addStretch(1);
    labelRow->addWidget(valueLabel);
    controlLayout->addLayout(labelRow);
    controlLayout->addWidget(slider);
    layout->addWidget(control);

    valueLabel->setText(valueText(slider));
    connect(slider, &QSlider::valueChanged, this,
            [this, slider, valueLabel](int) {
                valueLabel->setText(valueText(slider));
                handleSliderValueChanged(slider);
            });
    connect(slider, &QSlider::sliderReleased, this,
            [this, slider]() {
                handleSliderReleased(slider);
            });
    return slider;
}

void EffectsPanel::setSettings(const imaging::EffectsSettings &effectSettings)
{
    m_settingSettings = true;
    m_vignetteAmountSlider->setValue(
        std::clamp(effectSettings.vignetteAmount, -100, 100));
    m_vignetteMidpointSlider->setValue(
        std::clamp(effectSettings.vignetteMidpoint, 0, 100));
    m_vignetteRoundnessSlider->setValue(
        std::clamp(effectSettings.vignetteRoundness, -100, 100));
    m_vignetteFeatherSlider->setValue(
        std::clamp(effectSettings.vignetteFeather, 0, 100));
    m_vignetteHighlightsSlider->setValue(
        std::clamp(effectSettings.vignetteHighlights, 0, 100));
    m_grainAmountSlider->setValue(
        std::clamp(effectSettings.grainAmount, 0, 100));
    m_grainSizeSlider->setValue(
        std::clamp(effectSettings.grainSize, 0, 100));
    m_grainRoughnessSlider->setValue(
        std::clamp(effectSettings.grainRoughness, 0, 100));
    m_settingSettings = false;
}

imaging::EffectsSettings EffectsPanel::settings() const
{
    imaging::EffectsSettings result;
    result.vignetteAmount = m_vignetteAmountSlider->value();
    result.vignetteMidpoint = m_vignetteMidpointSlider->value();
    result.vignetteRoundness = m_vignetteRoundnessSlider->value();
    result.vignetteFeather = m_vignetteFeatherSlider->value();
    result.vignetteHighlights = m_vignetteHighlightsSlider->value();
    result.grainAmount = m_grainAmountSlider->value();
    result.grainSize = m_grainSizeSlider->value();
    result.grainRoughness = m_grainRoughnessSlider->value();
    return result;
}

bool EffectsPanel::isEditing() const
{
    return m_vignetteAmountSlider->isSliderDown()
        || m_vignetteMidpointSlider->isSliderDown()
        || m_vignetteRoundnessSlider->isSliderDown()
        || m_vignetteFeatherSlider->isSliderDown()
        || m_vignetteHighlightsSlider->isSliderDown()
        || m_grainAmountSlider->isSliderDown()
        || m_grainSizeSlider->isSliderDown()
        || m_grainRoughnessSlider->isSliderDown();
}

QString EffectsPanel::valueText(const QSlider *slider) const
{
    if (!slider) {
        return {};
    }

    const int value = slider->value();
    if (slider->property("signedValue").toBool() && value > 0) {
        return QStringLiteral("+%1").arg(value);
    }
    return QString::number(value);
}

QString EffectsPanel::historyLabelForSlider(const QSlider *slider) const
{
    if (!slider) {
        return tr("Effects");
    }
    return QStringLiteral("%1 %2")
        .arg(slider->property("historyName").toString(), valueText(slider));
}

void EffectsPanel::handleSliderValueChanged(QSlider *slider)
{
    if (m_settingSettings || !m_window || !slider
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.effects = settings();
    if (slider->isSliderDown()) {
        m_window->requestPreview(state);
    } else {
        m_window->requestFullRender(state, historyLabelForSlider(slider));
    }
}

void EffectsPanel::handleSliderReleased(QSlider *slider)
{
    if (m_settingSettings || !m_window || !slider
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.effects = settings();
    m_window->requestFullRender(state, historyLabelForSlider(slider));
}

void EffectsPanel::syncFromWindowState()
{
    if (!m_window) {
        return;
    }
    setSettings(m_window->m_requestedState.effects);
}

void EffectsPanel::applyRenderedEffects(const QString &filePath,
                                        const DevelopState &renderedState,
                                        bool preview,
                                        const QImage &image)
{
    if (!m_window || filePath != m_window->m_currentFilePath || image.isNull()) {
        return;
    }

    if (renderedState != m_window->m_requestedState) {
        return;
    }

    setSettings(m_window->m_requestedState.effects);
    if (!m_window->m_requestedState.effects.hasVisibleEffects()) {
        return;
    }

    double pixelScale = 1.0;
    if (preview && m_window->m_fullImageSize.isValid()
        && !m_window->m_fullImageSize.isEmpty()) {
        const double widthScale = static_cast<double>(image.width())
            / static_cast<double>(m_window->m_fullImageSize.width());
        const double heightScale = static_cast<double>(image.height())
            / static_cast<double>(m_window->m_fullImageSize.height());
        pixelScale = std::clamp(std::min(widthScale, heightScale), 0.01, 1.0);
    }

    const QImage processed = imaging::EffectsProcessor::applyForCanvas(
        image,
        m_window->m_requestedState,
        pixelScale);
    if (processed.isNull()) {
        return;
    }

    if (preview) {
        if (m_window->m_canvas) {
            m_window->m_canvas->replacePreviewImage(processed,
                                                    m_window->m_fullImageSize);
        }
    } else if (m_window->m_canvas) {
        m_window->m_canvas->replaceImage(processed);
    }

    if (m_window->m_navigator) {
        m_window->m_navigator->setImage(processed);
    }
    if (m_window->m_histogram) {
        m_window->m_histogram->setImage(processed);
    }
}
