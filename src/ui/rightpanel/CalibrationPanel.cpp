#include "ui/rightpanel/CalibrationPanel.h"
#include "ui/rightpanel/WheelIgnoringSlider.h"

#include <QChronoTimer>
#include <QElapsedTimer>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QSizePolicy>
#include <QSlider>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <functional>
#include <numbers>

namespace {

constexpr int kPanelAnimationDurationMs = 360;
constexpr int kPanelAnimationMinimumDurationMs = 90;
constexpr auto kPanelAnimationTick = std::chrono::nanoseconds(2'777'778);

qreal easedPanelProgress(qreal progress)
{
    progress = std::clamp(progress, 0.0, 1.0);
    return 0.5 - 0.5 * std::cos(progress * std::numbers::pi_v<qreal>);
}

QString signedValue(int value)
{
    return value > 0
        ? QStringLiteral("+%1").arg(value)
        : QString::number(value);
}

class CalibrationTitleButton final : public QToolButton
{
public:
    explicit CalibrationTitleButton(QWidget *parent = nullptr)
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

class CalibrationEyeButton final : public QToolButton
{
public:
    explicit CalibrationEyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Calibration adjustments visibility"));
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

class CalibrationChevronButton final : public QToolButton
{
public:
    explicit CalibrationChevronButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(26, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
    }

    void setExpansionProgress(qreal progress)
    {
        progress = std::clamp(progress, 0.0, 1.0);
        if (qFuzzyCompare(m_progress, progress)) {
            return;
        }

        m_progress = progress;
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

class RevealSnapshot final : public QWidget
{
public:
    explicit RevealSnapshot(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_NoSystemBackground, true);
        hide();
    }

    void setSnapshot(const QPixmap &pixmap, const QSize &logicalSize)
    {
        m_pixmap = pixmap;
        setFixedSize(logicalSize);
        update();
    }

    void clearSnapshot()
    {
        hide();
        m_pixmap = {};
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        if (m_pixmap.isNull()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
        painter.drawPixmap(0, 0, m_pixmap);
    }

private:
    QPixmap m_pixmap;
};

class RevealContainer final : public QWidget
{
public:
    explicit RevealContainer(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_timer(new QChronoTimer(this))
        , m_snapshot(new RevealSnapshot(this))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_timer->setTimerType(Qt::PreciseTimer);
        m_timer->setInterval(kPanelAnimationTick);
        connect(m_timer, &QChronoTimer::timeout, this,
                [this]() { advanceAnimation(); });
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

    void setProgressCallback(std::function<void(qreal)> callback)
    {
        m_progressCallback = std::move(callback);
        notifyProgress(height());
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

        if (!m_timer->isActive()) {
            setFixedHeight(m_expanded ? m_expandedHeight : 0);
            notifyProgress(height());
        }
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded && !m_timer->isActive()) {
            return;
        }

        m_timer->stop();

        if (expanded) {
            refreshExpandedHeight();
        }

        cacheContentPixmap();
        if (m_content) {
            m_content->hide();
        }
        if (!m_animationPixmap.isNull()) {
            m_snapshot->setSnapshot(m_animationPixmap,
                                    QSize(width(), m_expandedHeight));
            m_snapshot->show();
            m_snapshot->raise();
        }

        m_expanded = expanded;
        m_startHeight = static_cast<qreal>(height());
        m_endHeight = expanded ? static_cast<qreal>(m_expandedHeight) : 0.0;

        const qreal fullDistance = qMax<qreal>(1.0, m_expandedHeight);
        const qreal distanceFraction = std::clamp(
            std::abs(m_endHeight - m_startHeight) / fullDistance,
            0.0,
            1.0);
        const qint64 baseDurationNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds(kPanelAnimationDurationMs)).count();
        const qint64 minimumDurationNs =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::milliseconds(kPanelAnimationMinimumDurationMs)).count();
        m_durationNs = qMax<qint64>(
            minimumDurationNs,
            static_cast<qint64>(std::llround(baseDurationNs * distanceFraction)));

        if (std::abs(m_endHeight - m_startHeight) < 0.5) {
            finishAnimation();
            return;
        }

        m_elapsed.restart();
        m_timer->start();
        advanceAnimation();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);
        if (!m_timer->isActive() && event->size().width() != event->oldSize().width()) {
            updateContentGeometry();
        }
    }

private:
    void advanceAnimation()
    {
        if (!m_timer->isActive() || m_durationNs <= 0) {
            return;
        }

        const qint64 elapsedNs = m_elapsed.nsecsElapsed();
        const qreal linearProgress = std::clamp(
            static_cast<qreal>(elapsedNs) / static_cast<qreal>(m_durationNs),
            0.0,
            1.0);
        const qreal easedProgress = easedPanelProgress(linearProgress);
        const qreal animatedHeight = m_startHeight
            + (m_endHeight - m_startHeight) * easedProgress;
        const int nextHeight = qRound(animatedHeight);

        if (m_expanded) {
            if (height() != nextHeight) {
                setFixedHeight(nextHeight);
            }
        } else if (m_snapshot->height() != nextHeight) {
            m_snapshot->setFixedHeight(nextHeight);
        }
        notifyProgress(animatedHeight);

        if (linearProgress >= 1.0) {
            finishAnimation();
        }
    }

    void finishAnimation()
    {
        m_timer->stop();
        const int finalHeight = m_expanded ? m_expandedHeight : 0;
        if (height() != finalHeight) {
            setFixedHeight(finalHeight);
        }
        if (m_content) {
            m_content->setVisible(m_expanded);
        }
        m_snapshot->clearSnapshot();
        m_animationPixmap = {};
        notifyProgress(finalHeight);
        update();
    }

    void notifyProgress(qreal currentHeight)
    {
        if (!m_progressCallback) {
            return;
        }

        const qreal progress = m_expandedHeight > 0
            ? std::clamp(currentHeight / static_cast<qreal>(m_expandedHeight), 0.0, 1.0)
            : (m_expanded ? 1.0 : 0.0);
        m_progressCallback(progress);
    }

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

        const qreal devicePixelRatio = m_content->devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(m_content->width() * devicePixelRatio)),
                              qMax(1, qRound(m_content->height() * devicePixelRatio)));
        m_animationPixmap = QPixmap(pixelSize);
        m_animationPixmap.setDevicePixelRatio(devicePixelRatio);
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
    QChronoTimer *m_timer = nullptr;
    RevealSnapshot *m_snapshot = nullptr;
    QElapsedTimer m_elapsed;
    QPixmap m_animationPixmap;
    std::function<void(qreal)> m_progressCallback;
    qreal m_startHeight = 0.0;
    qreal m_endHeight = 0.0;
    qint64 m_durationNs = 0;
    int m_expandedHeight = 0;
    bool m_expanded = false;
};

class ResetLabel final : public QLabel
{
public:
    explicit ResetLabel(const QString &text,
                        QSlider *slider,
                        QWidget *parent = nullptr)
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

QLabel *sectionLabel(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("calibrationSectionLabel"));
    return label;
}

} // namespace

CalibrationPanel::CalibrationPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("calibrationPanel"));

    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("calibrationHeader"));
    header->setFixedHeight(40);

    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *titleButton = new CalibrationTitleButton(header);
    titleButton->setObjectName(QStringLiteral("calibrationTitle"));
    titleButton->setText(tr("Calibration"));
    titleButton->setCheckable(true);
    titleButton->setChecked(false);
    titleButton->setToolButtonStyle(Qt::ToolButtonTextOnly);
    titleButton->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    titleButton->setCursor(Qt::PointingHandCursor);
    titleButton->setFocusPolicy(Qt::NoFocus);

    auto *eyeButton = new CalibrationEyeButton(header);
    auto *chevronButton = new CalibrationChevronButton(header);
    chevronButton->setObjectName(QStringLiteral("calibrationChevron"));
    m_chevronButton = chevronButton;

    headerLayout->addWidget(titleButton, 1);
    headerLayout->addWidget(eyeButton);
    headerLayout->addWidget(chevronButton);
    outerLayout->addWidget(header);

    auto *revealContainer = new RevealContainer(this);
    m_content = new QFrame(revealContainer);
    m_content->setObjectName(QStringLiteral("calibrationContent"));
    auto *contentLayout = new QVBoxLayout(m_content);
    contentLayout->setContentsMargins(12, 10, 12, 16);
    contentLayout->setSpacing(6);

    contentLayout->addWidget(sectionLabel(tr("Shadows"), m_content));
    m_shadowsTintSlider = addSlider(m_content,
                                    contentLayout,
                                    tr("Tint"),
                                    tr("Shadows Tint"));
    m_shadowsTintSlider->setProperty("gradientRole", QStringLiteral("shadowsTint"));

    contentLayout->addSpacing(7);
    contentLayout->addWidget(sectionLabel(tr("Red Primary"), m_content));
    m_redHueSlider = addSlider(m_content,
                               contentLayout,
                               tr("Hue"),
                               tr("Red Primary Hue"));
    m_redHueSlider->setProperty("gradientRole", QStringLiteral("redHue"));
    m_redSaturationSlider = addSlider(m_content,
                                      contentLayout,
                                      tr("Saturation"),
                                      tr("Red Primary Saturation"));
    m_redSaturationSlider->setProperty("gradientRole", QStringLiteral("redSaturation"));

    contentLayout->addSpacing(7);
    contentLayout->addWidget(sectionLabel(tr("Green Primary"), m_content));
    m_greenHueSlider = addSlider(m_content,
                                 contentLayout,
                                 tr("Hue"),
                                 tr("Green Primary Hue"));
    m_greenHueSlider->setProperty("gradientRole", QStringLiteral("greenHue"));
    m_greenSaturationSlider = addSlider(m_content,
                                        contentLayout,
                                        tr("Saturation"),
                                        tr("Green Primary Saturation"));
    m_greenSaturationSlider->setProperty("gradientRole", QStringLiteral("greenSaturation"));

    contentLayout->addSpacing(7);
    contentLayout->addWidget(sectionLabel(tr("Blue Primary"), m_content));
    m_blueHueSlider = addSlider(m_content,
                                contentLayout,
                                tr("Hue"),
                                tr("Blue Primary Hue"));
    m_blueHueSlider->setProperty("gradientRole", QStringLiteral("blueHue"));
    m_blueSaturationSlider = addSlider(m_content,
                                       contentLayout,
                                       tr("Saturation"),
                                       tr("Blue Primary Saturation"));
    m_blueSaturationSlider->setProperty("gradientRole", QStringLiteral("blueSaturation"));

    revealContainer->setContentWidget(m_content);
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
        "QWidget#calibrationPanel {"
        "  background: #181818;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QFrame#calibrationHeader {"
        "  background: transparent;"
        "  border: 0;"
        "  border-top: 1px solid #2a2a2a;"
        "}"
        "QToolButton#calibrationTitle {"
        "  color: #d6d6d6;"
        "  background: transparent;"
        "  border: 0;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "  padding: 0;"
        "}"
        "QToolButton#calibrationTitle:hover { color: #ffffff; }"
        "QToolButton#calibrationChevron {"
        "  color: #bebebe;"
        "  background: transparent;"
        "  border: 0;"
        "}"
        "QToolButton#calibrationChevron:hover { color: #ffffff; }"
        "QFrame#calibrationContent, QWidget#calibrationControl {"
        "  background: #181818;"
        "  border: 0;"
        "}"
        "QLabel#calibrationSectionLabel {"
        "  color: #b3b3b3;"
        "  font-size: 11px;"
        "  font-weight: 600;"
        "  padding-top: 1px;"
        "  padding-bottom: 1px;"
        "}"
        "QLabel#calibrationControlLabel {"
        "  color: #d0d0d0;"
        "  font-size: 12px;"
        "}"
        "QLabel#calibrationValueLabel {"
        "  color: #9f9f9f;"
        "  font-size: 11px;"
        "}"
        "QSlider#calibrationSlider {"
        "  min-height: 14px;"
        "  max-height: 14px;"
        "}"
        "QSlider#calibrationSlider::groove:horizontal {"
        "  height: 2px;"
        "  border-radius: 1px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #303030, stop:0.5 #626262, stop:1 #9b9b9b);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"shadowsTint\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #38b96b, stop:0.5 #696969, stop:1 #c94fb3);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"redHue\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #c843a1, stop:0.5 #d84d4d, stop:1 #e9953f);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"redSaturation\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #555555, stop:0.5 #8c4646, stop:1 #d95650);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"greenHue\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #b8a83f, stop:0.5 #48aa58, stop:1 #3db6a4);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"greenSaturation\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #555555, stop:0.5 #477b4e, stop:1 #4db45e);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"blueHue\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #43b7b5, stop:0.5 #4779c4, stop:1 #7950bd);"
        "}"
        "QSlider#calibrationSlider[gradientRole=\"blueSaturation\"]::groove:horizontal {"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #555555, stop:0.5 #466d7d, stop:1 #42aabd);"
        "}"
        "QSlider#calibrationSlider::sub-page:horizontal,"
        "QSlider#calibrationSlider::add-page:horizontal { background: transparent; }"
        "QSlider#calibrationSlider::handle:horizontal {"
        "  width: 10px;"
        "  height: 10px;"
        "  margin: -4px 0;"
        "  border-radius: 5px;"
        "  background: #d9d9d9;"
        "  border: 0;"
        "}"
        "QSlider#calibrationSlider::handle:horizontal:hover { background: #ffffff; }"));

    revealContainer->refreshExpandedHeight();
}

QSlider *CalibrationPanel::addSlider(QFrame *content,
                                     QVBoxLayout *layout,
                                     const QString &labelText,
                                     const QString &historyName)
{
    auto *control = new QWidget(content);
    control->setObjectName(QStringLiteral("calibrationControl"));

    auto *controlLayout = new QVBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(2);

    auto *labelRow = new QHBoxLayout;
    labelRow->setContentsMargins(0, 0, 0, 0);
    labelRow->setSpacing(8);

    auto *slider = new WheelIgnoringSlider(Qt::Horizontal, control);
    slider->setObjectName(QStringLiteral("calibrationSlider"));
    slider->setRange(-100, 100);
    slider->setValue(0);
    slider->setTracking(true);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    slider->setProperty("historyName", historyName);

    auto *label = new ResetLabel(labelText, slider, control);
    label->setObjectName(QStringLiteral("calibrationControlLabel"));

    auto *valueLabel = new QLabel(QStringLiteral("0"), control);
    valueLabel->setObjectName(QStringLiteral("calibrationValueLabel"));
    valueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    labelRow->addWidget(label);
    labelRow->addStretch(1);
    labelRow->addWidget(valueLabel);
    controlLayout->addLayout(labelRow);
    controlLayout->addWidget(slider);
    layout->addWidget(control);

    connect(slider, &QSlider::valueChanged, this,
            [this, slider, valueLabel](int value) {
                valueLabel->setText(signedValue(value));
                handleSliderValueChanged(slider);
            });
    connect(slider, &QSlider::sliderReleased, this,
            [this, slider]() {
                handleSliderReleased(slider);
            });

    return slider;
}

void CalibrationPanel::setCalibration(const imaging::RawCalibration &calibration)
{
    m_settingCalibration = true;
    m_shadowsTintSlider->setValue(std::clamp(calibration.shadowsTint, -100, 100));
    m_redHueSlider->setValue(std::clamp(calibration.redPrimaryHue, -100, 100));
    m_redSaturationSlider->setValue(
        std::clamp(calibration.redPrimarySaturation, -100, 100));
    m_greenHueSlider->setValue(std::clamp(calibration.greenPrimaryHue, -100, 100));
    m_greenSaturationSlider->setValue(
        std::clamp(calibration.greenPrimarySaturation, -100, 100));
    m_blueHueSlider->setValue(std::clamp(calibration.bluePrimaryHue, -100, 100));
    m_blueSaturationSlider->setValue(
        std::clamp(calibration.bluePrimarySaturation, -100, 100));
    m_settingCalibration = false;
}

imaging::RawCalibration CalibrationPanel::calibration() const
{
    imaging::RawCalibration result;
    result.shadowsTint = m_shadowsTintSlider->value();
    result.redPrimaryHue = m_redHueSlider->value();
    result.redPrimarySaturation = m_redSaturationSlider->value();
    result.greenPrimaryHue = m_greenHueSlider->value();
    result.greenPrimarySaturation = m_greenSaturationSlider->value();
    result.bluePrimaryHue = m_blueHueSlider->value();
    result.bluePrimarySaturation = m_blueSaturationSlider->value();
    return result;
}

bool CalibrationPanel::isEditing() const
{
    return m_shadowsTintSlider->isSliderDown()
        || m_redHueSlider->isSliderDown()
        || m_redSaturationSlider->isSliderDown()
        || m_greenHueSlider->isSliderDown()
        || m_greenSaturationSlider->isSliderDown()
        || m_blueHueSlider->isSliderDown()
        || m_blueSaturationSlider->isSliderDown();
}

QString CalibrationPanel::historyLabelForSlider(const QSlider *slider) const
{
    if (!slider) {
        return tr("Calibration");
    }

    return QStringLiteral("%1 %2")
        .arg(slider->property("historyName").toString(),
             signedValue(slider->value()));
}

void CalibrationPanel::handleSliderValueChanged(QSlider *slider)
{
    if (m_settingCalibration || !slider) {
        return;
    }

    if (slider->isSliderDown()) {
        emit previewRequested(calibration());
    } else {
        emit commitRequested(calibration(), historyLabelForSlider(slider));
    }
}

void CalibrationPanel::handleSliderReleased(QSlider *slider)
{
    if (m_settingCalibration || !slider) {
        return;
    }

    emit commitRequested(calibration(), historyLabelForSlider(slider));
}
