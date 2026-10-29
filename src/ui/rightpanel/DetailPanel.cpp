#include "ui/rightpanel/DetailPanel.h"

#include "app/MainWindow.h"
#include "imaging/TemperatureRenderService.h"
#include "ui/rightpanel/WheelIgnoringSlider.h"

#include <QEasingCurve>
#include <QFrame>
#include <QHBoxLayout>
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

class DetailTitleButton final : public QToolButton
{
public:
    explicit DetailTitleButton(QWidget *parent = nullptr)
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

class DetailEyeButton final : public QToolButton
{
public:
    explicit DetailEyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Detail adjustments visibility"));
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

class DetailChevronButton final : public QToolButton
{
public:
    explicit DetailChevronButton(QWidget *parent = nullptr)
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

class RevealContainer final : public QWidget
{
public:
    explicit RevealContainer(QWidget *parent = nullptr)
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

class ResetLabel final : public QLabel
{
public:
    ResetLabel(const QString &text,
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
    label->setObjectName(QStringLiteral("detailSectionLabel"));
    return label;
}

} // namespace

DetailPanel *DetailPanel::install(MainWindow *window)
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
            QStringLiteral("detailPanel"), Qt::FindDirectChildrenOnly)) {
        return static_cast<DetailPanel *>(existing);
    }

    auto *panel = new DetailPanel(window, container);
    QWidget *calibrationPanel = container->findChild<QWidget *>(
        QStringLiteral("calibrationPanel"), Qt::FindDirectChildrenOnly);
    const int calibrationIndex = calibrationPanel ? layout->indexOf(calibrationPanel) : -1;
    if (calibrationIndex >= 0) {
        layout->insertWidget(calibrationIndex, panel);
    } else {
        layout->insertWidget(std::max(0, layout->count() - 1), panel);
    }
    return panel;
}

DetailPanel::DetailPanel(MainWindow *window, QWidget *parent)
    : QWidget(parent)
    , m_window(window)
{
    setObjectName(QStringLiteral("detailPanel"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("detailHeader"));
    header->setFixedHeight(40);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *titleButton = new DetailTitleButton(header);
    titleButton->setObjectName(QStringLiteral("detailTitle"));
    titleButton->setText(tr("Detail"));
    titleButton->setCheckable(true);
    titleButton->setChecked(false);
    titleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    titleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    titleButton->setCursor(Qt::PointingHandCursor);
    titleButton->setFocusPolicy(Qt::NoFocus);

    auto *eyeButton = new DetailEyeButton(header);
    auto *chevronButton = new DetailChevronButton(header);

    headerLayout->addWidget(titleButton, 1);
    headerLayout->addWidget(eyeButton);
    headerLayout->addWidget(chevronButton);
    outerLayout->addWidget(header);

    auto *revealContainer = new RevealContainer(this);
    auto *content = new QFrame(revealContainer);
    content->setObjectName(QStringLiteral("detailContent"));
    auto *contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(12, 10, 12, 16);
    contentLayout->setSpacing(6);

    contentLayout->addWidget(sectionLabel(tr("Sharpening"), content));
    m_sharpeningAmountSlider = addSlider(content,
                                          contentLayout,
                                          tr("Amount"),
                                          tr("Sharpening Amount"),
                                          0,
                                          150,
                                          0);
    m_sharpeningRadiusSlider = addSlider(content,
                                          contentLayout,
                                          tr("Radius"),
                                          tr("Sharpening Radius"),
                                          50,
                                          300,
                                          100,
                                          true);
    m_sharpeningDetailSlider = addSlider(content,
                                          contentLayout,
                                          tr("Detail"),
                                          tr("Sharpening Detail"),
                                          0,
                                          100,
                                          25);
    m_sharpeningMaskingSlider = addSlider(content,
                                           contentLayout,
                                           tr("Masking"),
                                           tr("Sharpening Masking"),
                                           0,
                                           100,
                                           0);

    contentLayout->addSpacing(7);
    contentLayout->addWidget(sectionLabel(tr("Noise Reduction"), content));
    m_luminanceNoiseReductionSlider = addSlider(content,
                                                 contentLayout,
                                                 tr("Luminance"),
                                                 tr("Luminance Noise Reduction"),
                                                 0,
                                                 100,
                                                 0);
    m_colorNoiseReductionSlider = addSlider(content,
                                             contentLayout,
                                             tr("Color"),
                                             tr("Color Noise Reduction"),
                                             0,
                                             100,
                                             0);

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
        "QWidget#detailPanel { background: #181818; font-family: \"Segoe UI Variable\"; }"
        "QFrame#detailHeader { background: transparent; border: 0; border-top: 1px solid #2a2a2a; }"
        "QToolButton#detailTitle { color: #d6d6d6; background: transparent; border: 0; font-size: 13px; font-weight: 600; padding: 0; }"
        "QToolButton#detailTitle:hover { color: #ffffff; }"
        "QFrame#detailContent, QWidget#detailControl { background: #181818; border: 0; }"
        "QLabel#detailSectionLabel { color: #b3b3b3; font-size: 11px; font-weight: 600; padding-top: 1px; padding-bottom: 1px; }"
        "QLabel#detailControlLabel { color: #d0d0d0; font-size: 12px; }"
        "QLabel#detailValueLabel { color: #9f9f9f; font-size: 11px; }"
        "QSlider#detailSlider { min-height: 14px; max-height: 14px; }"
        "QSlider#detailSlider::groove:horizontal { height: 2px; border-radius: 1px; background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #303030, stop:0.5 #626262, stop:1 #9b9b9b); }"
        "QSlider#detailSlider[historyName=\"Sharpening Amount\"]::groove:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #303030, stop:0.5 #7f3b3b, stop:1 #d95a5a); }"
        "QSlider#detailSlider[historyName=\"Sharpening Masking\"]::groove:horizontal { background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #9b9b9b, stop:0.5 #626262, stop:1 #303030); }"
        "QSlider#detailSlider::sub-page:horizontal, QSlider#detailSlider::add-page:horizontal { background: transparent; }"
        "QSlider#detailSlider::handle:horizontal { width: 10px; height: 10px; margin: -4px 0; border-radius: 5px; background: #d9d9d9; border: 0; }"
        "QSlider#detailSlider::handle:horizontal:hover { background: #ffffff; }"));

    if (m_window) {
        if (auto *historyList = m_window->findChild<QListWidget *>(
                QStringLiteral("historyList"))) {
            connect(historyList, &QListWidget::currentRowChanged, this,
                    [this](int) {
                        syncFromWindowState();
                    });
        }
    }
    syncFromWindowState();
}

QSlider *DetailPanel::addSlider(QFrame *content,
                                QVBoxLayout *layout,
                                const QString &labelText,
                                const QString &historyName,
                                int minimum,
                                int maximum,
                                int defaultValue,
                                bool radiusFormat)
{
    auto *control = new QWidget(content);
    control->setObjectName(QStringLiteral("detailControl"));
    auto *controlLayout = new QVBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(2);

    auto *labelRow = new QHBoxLayout;
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(8);

    auto *slider = new WheelIgnoringSlider(Qt::Horizontal, control);
    slider->setObjectName(QStringLiteral("detailSlider"));
    slider->setRange(minimum, maximum);
    slider->setValue(defaultValue);
    slider->setTracking(true);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    slider->setProperty("historyName", historyName);
    slider->setProperty("radiusFormat", radiusFormat);

    auto *label = new ResetLabel(labelText, slider, defaultValue, control);
    label->setObjectName(QStringLiteral("detailControlLabel"));

    auto *valueLabel = new QLabel(control);
    valueLabel->setObjectName(QStringLiteral("detailValueLabel"));
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

void DetailPanel::setSettings(const imaging::DetailSettings &detailSettings)
{
    m_settingSettings = true;
    m_sharpeningAmountSlider->setValue(
        std::clamp(detailSettings.sharpeningAmount, 0, 150));
    m_sharpeningRadiusSlider->setValue(
        std::clamp(detailSettings.sharpeningRadiusHundredths, 50, 300));
    m_sharpeningDetailSlider->setValue(
        std::clamp(detailSettings.sharpeningDetail, 0, 100));
    m_sharpeningMaskingSlider->setValue(
        std::clamp(detailSettings.sharpeningMasking, 0, 100));
    m_luminanceNoiseReductionSlider->setValue(
        std::clamp(detailSettings.luminanceNoiseReduction, 0, 100));
    m_colorNoiseReductionSlider->setValue(
        std::clamp(detailSettings.colorNoiseReduction, 0, 100));
    m_settingSettings = false;
}

imaging::DetailSettings DetailPanel::settings() const
{
    imaging::DetailSettings result;
    result.sharpeningAmount = m_sharpeningAmountSlider->value();
    result.sharpeningRadiusHundredths = m_sharpeningRadiusSlider->value();
    result.sharpeningDetail = m_sharpeningDetailSlider->value();
    result.sharpeningMasking = m_sharpeningMaskingSlider->value();
    result.luminanceNoiseReduction = m_luminanceNoiseReductionSlider->value();
    result.colorNoiseReduction = m_colorNoiseReductionSlider->value();
    return result;
}

bool DetailPanel::isEditing() const
{
    return m_sharpeningAmountSlider->isSliderDown()
        || m_sharpeningRadiusSlider->isSliderDown()
        || m_sharpeningDetailSlider->isSliderDown()
        || m_sharpeningMaskingSlider->isSliderDown()
        || m_luminanceNoiseReductionSlider->isSliderDown()
        || m_colorNoiseReductionSlider->isSliderDown();
}

QString DetailPanel::valueText(const QSlider *slider) const
{
    if (!slider) {
        return {};
    }
    if (slider->property("radiusFormat").toBool()) {
        return QString::number(static_cast<double>(slider->value()) / 100.0, 'f', 1);
    }
    return QString::number(slider->value());
}

QString DetailPanel::historyLabelForSlider(const QSlider *slider) const
{
    if (!slider) {
        return tr("Detail");
    }
    return QStringLiteral("%1 %2")
        .arg(slider->property("historyName").toString(), valueText(slider));
}

void DetailPanel::handleSliderValueChanged(QSlider *slider)
{
    if (m_settingSettings || !m_window || !slider
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.detail = settings();
    if (m_window->m_renderService) {
        m_window->m_renderService->setDetailSettings(m_window->m_currentFilePath,
                                                      state.detail);
    }

    if (slider->isSliderDown()) {
        m_window->requestPreview(state);
    } else {
        m_window->requestFullRender(state, historyLabelForSlider(slider));
    }
}

void DetailPanel::handleSliderReleased(QSlider *slider)
{
    if (m_settingSettings || !m_window || !slider
        || m_window->m_currentFilePath.isEmpty()) {
        return;
    }

    DevelopState state = m_window->m_requestedState;
    state.detail = settings();
    if (m_window->m_renderService) {
        m_window->m_renderService->setDetailSettings(m_window->m_currentFilePath,
                                                      state.detail);
    }
    m_window->requestFullRender(state, historyLabelForSlider(slider));
}

void DetailPanel::syncFromWindowState()
{
    if (!m_window) {
        return;
    }

    setSettings(m_window->m_requestedState.detail);
    if (m_window->m_renderService && !m_window->m_currentFilePath.isEmpty()) {
        m_window->m_renderService->setDetailSettings(m_window->m_currentFilePath,
                                                      m_window->m_requestedState.detail);
    }
}
