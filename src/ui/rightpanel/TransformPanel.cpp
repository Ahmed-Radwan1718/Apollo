#include "ui/rightpanel/TransformPanel.h"

#include "imaging/UprightAnalysis.h"
#include "ui/rightpanel/WheelIgnoringSlider.h"

#include <QAbstractButton>
#include <QButtonGroup>
#include <QCheckBox>
#include <QChronoTimer>
#include <QElapsedTimer>
#include <QFrame>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QResizeEvent>
#include <QSignalBlocker>
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

class TransformTitleButton final : public QToolButton
{
public:
    explicit TransformTitleButton(QWidget *parent = nullptr) : QToolButton(parent) {}

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

class TransformEyeButton final : public QToolButton
{
public:
    explicit TransformEyeButton(QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setFixedSize(28, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Transform adjustments visibility"));
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

class TransformChevronButton final : public QToolButton
{
public:
    explicit TransformChevronButton(QWidget *parent = nullptr) : QToolButton(parent)
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
        painter.setPen(QPen(underMouse() ? QColor(255, 255, 255) : QColor(190, 190, 190),
                            1.45, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
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
    explicit RevealSnapshot(QWidget *parent = nullptr) : QWidget(parent)
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
        if (m_pixmap.isNull()) return;
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
        connect(m_timer, &QChronoTimer::timeout, this, [this]() { advanceAnimation(); });
    }

    void setContentWidget(QWidget *content)
    {
        m_content = content;
        if (!m_content) return;
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
        if (!m_content) return;
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
            m_content->setVisible(m_expanded);
            notifyProgress(height());
        }
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded && !m_timer->isActive()) return;
        m_timer->stop();
        if (expanded) refreshExpandedHeight();
        cacheContentPixmap();
        if (m_content) m_content->hide();
        if (!m_animationPixmap.isNull()) {
            m_snapshot->setSnapshot(m_animationPixmap, QSize(width(), m_expandedHeight));
            m_snapshot->show();
            m_snapshot->raise();
        }

        m_expanded = expanded;
        m_startHeight = static_cast<qreal>(height());
        m_endHeight = expanded ? static_cast<qreal>(m_expandedHeight) : 0.0;
        const qreal fraction = std::clamp(
            std::abs(m_endHeight - m_startHeight) / qMax<qreal>(1.0, m_expandedHeight),
            0.0, 1.0);
        const qint64 fullNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds(kPanelAnimationDurationMs)).count();
        const qint64 minimumNs = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::milliseconds(kPanelAnimationMinimumDurationMs)).count();
        m_durationNs = qMax(minimumNs,
                            static_cast<qint64>(std::llround(fullNs * fraction)));
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
        if (!m_timer->isActive() || m_durationNs <= 0) return;
        const qreal linear = std::clamp(
            static_cast<qreal>(m_elapsed.nsecsElapsed()) / static_cast<qreal>(m_durationNs),
            0.0, 1.0);
        const qreal eased = easedPanelProgress(linear);
        const int nextHeight = qRound(m_startHeight + (m_endHeight - m_startHeight) * eased);
        if (m_expanded) {
            if (height() != nextHeight) setFixedHeight(nextHeight);
        } else if (m_snapshot->height() != nextHeight) {
            m_snapshot->setFixedHeight(nextHeight);
        }
        notifyProgress(nextHeight);
        if (linear >= 1.0) finishAnimation();
    }

    void finishAnimation()
    {
        m_timer->stop();
        const int finalHeight = m_expanded ? m_expandedHeight : 0;
        setFixedHeight(finalHeight);
        if (m_content) m_content->setVisible(m_expanded);
        m_snapshot->clearSnapshot();
        m_animationPixmap = {};
        notifyProgress(finalHeight);
    }

    void notifyProgress(qreal currentHeight)
    {
        if (!m_progressCallback) return;
        const qreal progress = m_expandedHeight > 0
            ? std::clamp(currentHeight / static_cast<qreal>(m_expandedHeight), 0.0, 1.0)
            : (m_expanded ? 1.0 : 0.0);
        m_progressCallback(progress);
    }

    void cacheContentPixmap()
    {
        if (!m_content || !m_animationPixmap.isNull() || m_expandedHeight <= 0) return;
        const bool wasHidden = m_content->isHidden();
        if (wasHidden) m_content->show();
        updateContentGeometry();
        m_content->ensurePolished();
        const qreal dpr = m_content->devicePixelRatioF();
        const QSize pixels(qMax(1, qRound(m_content->width() * dpr)),
                           qMax(1, qRound(m_content->height() * dpr)));
        m_animationPixmap = QPixmap(pixels);
        m_animationPixmap.setDevicePixelRatio(dpr);
        m_animationPixmap.fill(Qt::transparent);
        m_content->render(&m_animationPixmap);
        if (wasHidden) m_content->hide();
    }

    void updateContentGeometry()
    {
        if (m_content) m_content->setGeometry(0, 0, width(), m_expandedHeight);
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
    ResetLabel(const QString &text, QSlider *slider, int neutral, QWidget *parent = nullptr)
        : QLabel(text, parent), m_slider(slider), m_neutral(neutral) {}

protected:
    void mouseDoubleClickEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton && m_slider) {
            const bool wasDown = m_slider->isSliderDown();
            if (!wasDown) m_slider->setSliderDown(true);
            m_slider->setValue(m_neutral);
            if (!wasDown) m_slider->setSliderDown(false);
            event->accept();
            return;
        }
        QLabel::mouseDoubleClickEvent(event);
    }

private:
    QSlider *m_slider = nullptr;
    int m_neutral = 0;
};

QString formattedValue(int value, TransformPanel::ValueFormat format)
{
    switch (format) {
    case TransformPanel::ValueFormat::Degrees:
        return QStringLiteral("%1°").arg(value / 100.0, 0, 'f', 2);
    case TransformPanel::ValueFormat::Scale:
        return QString::number(value);
    case TransformPanel::ValueFormat::Integer:
    default:
        return value > 0 ? QStringLiteral("+%1").arg(value) : QString::number(value);
    }
}

} // namespace

TransformPanel::TransformPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("transformPanel"));
    auto *outerLayout = new QVBoxLayout(this);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto *header = new QFrame(this);
    header->setObjectName(QStringLiteral("transformHeader"));
    header->setFixedHeight(40);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(6, 5, 0, 0);
    headerLayout->setSpacing(2);

    auto *title = new TransformTitleButton(header);
    title->setObjectName(QStringLiteral("transformTitle"));
    title->setText(tr("Transform"));
    title->setCheckable(true);
    title->setChecked(false);
    title->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    title->setCursor(Qt::PointingHandCursor);
    title->setFocusPolicy(Qt::NoFocus);

    auto *eye = new TransformEyeButton(header);

    auto *chevron = new TransformChevronButton(header);
    chevron->setObjectName(QStringLiteral("transformChevron"));
    m_chevronButton = chevron;

    headerLayout->addWidget(title, 1);
    headerLayout->addWidget(eye);
    headerLayout->addWidget(chevron);
    outerLayout->addWidget(header);

    auto *reveal = new RevealContainer(this);
    m_content = new QFrame(reveal);
    m_content->setObjectName(QStringLiteral("transformContent"));
    auto *layout = new QVBoxLayout(m_content);
    layout->setContentsMargins(12, 10, 12, 16);
    layout->setSpacing(6);

    auto *uprightLabel = new QLabel(tr("Upright"), m_content);
    uprightLabel->setObjectName(QStringLiteral("transformSectionLabel"));
    layout->addWidget(uprightLabel);

    auto *uprightGrid = new QFrame(m_content);
    uprightGrid->setObjectName(QStringLiteral("uprightGrid"));
    auto *uprightLayout = new QGridLayout(uprightGrid);
    uprightLayout->setContentsMargins(0, 0, 0, 0);
    uprightLayout->setHorizontalSpacing(0);
    uprightLayout->setVerticalSpacing(0);
    uprightLayout->setColumnStretch(0, 1);
    uprightLayout->setColumnStretch(1, 1);
    uprightLayout->setColumnStretch(2, 1);

    m_uprightButtons = new QButtonGroup(this);
    m_uprightButtons->setExclusive(true);
    const auto addUprightButton = [this, uprightGrid, uprightLayout](
                                      const QString &text,
                                      imaging::UprightMode mode,
                                      int row,
                                      int column) {
        auto *button = new QToolButton(uprightGrid);
        button->setObjectName(QStringLiteral("uprightCell"));
        button->setText(text);
        button->setCheckable(true);
        button->setAutoExclusive(true);
        button->setFixedHeight(26);
        button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        button->setCursor(Qt::PointingHandCursor);
        button->setFocusPolicy(Qt::NoFocus);
        button->setProperty("rightDivider", column < 2);
        button->setProperty("bottomDivider", row == 0);
        const int id = static_cast<int>(mode);
        m_uprightButtons->addButton(button, id);
        uprightLayout->addWidget(button, row, column);
        return button;
    };

    addUprightButton(tr("Off"), imaging::UprightMode::Off, 0, 0)->setChecked(true);
    addUprightButton(tr("Auto"), imaging::UprightMode::Auto, 0, 1);
    addUprightButton(tr("Guided"), imaging::UprightMode::Guided, 0, 2);
    addUprightButton(tr("Level"), imaging::UprightMode::Level, 1, 0);
    addUprightButton(tr("Vertical"), imaging::UprightMode::Vertical, 1, 1);
    addUprightButton(tr("Full"), imaging::UprightMode::Full, 1, 2);
    layout->addWidget(uprightGrid);
    layout->addSpacing(4);

    m_verticalSlider = addSlider(m_content, layout, tr("Vertical"), -100, 100, 0,
                                 ValueFormat::Integer, tr("Transform Vertical"));
    m_horizontalSlider = addSlider(m_content, layout, tr("Horizontal"), -100, 100, 0,
                                   ValueFormat::Integer, tr("Transform Horizontal"));
    m_rotateSlider = addSlider(m_content, layout, tr("Rotate"), -1000, 1000, 0,
                               ValueFormat::Degrees, tr("Transform Rotate"));
    m_rotateSlider->setSingleStep(5);
    m_rotateSlider->setPageStep(25);
    m_aspectSlider = addSlider(m_content, layout, tr("Aspect"), -100, 100, 0,
                               ValueFormat::Integer, tr("Transform Aspect"));
    m_scaleSlider = addSlider(m_content, layout, tr("Scale"), 50, 150, 100,
                              ValueFormat::Scale, tr("Transform Scale"));
    m_xOffsetSlider = addSlider(m_content, layout, tr("X Offset"), -100, 100, 0,
                                ValueFormat::Integer, tr("Transform X Offset"));
    m_yOffsetSlider = addSlider(m_content, layout, tr("Y Offset"), -100, 100, 0,
                                ValueFormat::Integer, tr("Transform Y Offset"));

    m_constrainCrop = new QCheckBox(tr("Constrain Crop"), m_content);
    m_constrainCrop->setObjectName(QStringLiteral("transformConstrainCrop"));
    m_constrainCrop->setCursor(Qt::PointingHandCursor);
    layout->addSpacing(3);
    layout->addWidget(m_constrainCrop);

    reveal->setContentWidget(m_content);
    reveal->setProgressCallback([chevron](qreal progress) {
        chevron->setExpansionProgress(progress);
    });
    outerLayout->addWidget(reveal);

    connect(title, &QToolButton::toggled, reveal, [reveal](bool expanded) {
        reveal->setExpanded(expanded);
    });
    connect(chevron, &QToolButton::clicked, title, [title]() { title->toggle(); });
    connect(m_uprightButtons, &QButtonGroup::idClicked,
            this, [this](int mode) {
                if (m_settingControls) return;
                m_uprightMode = std::clamp(mode, 0, 5);
                emit uprightModeRequested(m_uprightMode);
            });
    connect(m_constrainCrop, &QCheckBox::toggled, this, [this](bool) {
        if (m_settingControls) return;
        emit previewRequested();
        emit commitRequested(tr("Transform Constrain Crop"));
    });

    setStyleSheet(QStringLiteral(
        "QWidget#transformPanel { background:#181818; font-family:\"Segoe UI Variable\"; }"
        "QFrame#transformHeader { background:transparent; border:0; border-top:1px solid #2a2a2a; }"
        "QToolButton#transformTitle { color:#d6d6d6; background:transparent; border:0; font-size:13px; font-weight:600; padding:0; }"
        "QToolButton#transformTitle:hover { color:#ffffff; }"
        "QToolButton#transformChevron { color:#bebebe; background:transparent; border:0; }"
        "QFrame#transformContent, QWidget#transformControl { background:#181818; border:0; }"
        "QLabel#transformSectionLabel { color:#b3b3b3; font-size:11px; font-weight:600; padding:1px 0; }"
        "QFrame#uprightGrid { background:transparent; border:0; }"
        "QToolButton#uprightCell { color:#b9b9b9; background:transparent; border:0; font-size:11px; padding:0 3px; }"
        "QToolButton#uprightCell[rightDivider=\"true\"] { border-right:1px solid #2b2b2b; }"
        "QToolButton#uprightCell[bottomDivider=\"true\"] { border-bottom:1px solid #2b2b2b; }"
        "QToolButton#uprightCell:hover { color:#f0f0f0; background:transparent; }"
        "QToolButton#uprightCell:checked { font-weight:bold; background:transparent; }"
        "QLabel#transformControlLabel { color:#d0d0d0; font-size:12px; }"
        "QLabel#transformValueLabel { color:#9f9f9f; font-size:11px; }"
        "QSlider#transformSlider { min-height:14px; max-height:14px; }"
        "QSlider#transformSlider::groove:horizontal { height:2px; border-radius:1px; background:#4d4d4d; }"
        "QSlider#transformSlider::sub-page:horizontal, QSlider#transformSlider::add-page:horizontal { background:transparent; }"
        "QSlider#transformSlider::handle:horizontal { width:10px; height:10px; margin:-4px 0; border-radius:5px; background:#d9d9d9; border:0; }"
        "QSlider#transformSlider::handle:horizontal:hover { background:#ffffff; }"
        "QCheckBox#transformConstrainCrop { color:#d0d0d0; spacing:7px; font-size:12px; padding-top:3px; }"
        "QCheckBox#transformConstrainCrop::indicator { width:13px; height:13px; }"));

    reveal->refreshExpandedHeight();
}

QSlider *TransformPanel::addSlider(QFrame *content,
                                   QVBoxLayout *layout,
                                   const QString &labelText,
                                   int minimum,
                                   int maximum,
                                   int neutral,
                                   ValueFormat format,
                                   const QString &historyLabel)
{
    auto *control = new QWidget(content);
    control->setObjectName(QStringLiteral("transformControl"));
    auto *controlLayout = new QVBoxLayout(control);
    controlLayout->setContentsMargins(0, 0, 0, 0);
    controlLayout->setSpacing(2);

    auto *row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(8);

    auto *slider = new WheelIgnoringSlider(Qt::Horizontal, control);
    slider->setObjectName(QStringLiteral("transformSlider"));
    slider->setRange(minimum, maximum);
    slider->setValue(neutral);
    slider->setTracking(true);
    slider->setProperty("historyLabel", historyLabel);
    slider->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *label = new ResetLabel(labelText, slider, neutral, control);
    label->setObjectName(QStringLiteral("transformControlLabel"));
    auto *value = new QLabel(formattedValue(neutral, format), control);
    value->setObjectName(QStringLiteral("transformValueLabel"));
    value->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    row->addWidget(label);
    row->addStretch(1);
    row->addWidget(value);
    controlLayout->addLayout(row);
    controlLayout->addWidget(slider);
    layout->addWidget(control);

    connect(slider, &QSlider::valueChanged, this,
            [this, slider, value, format](int next) {
                value->setText(formattedValue(next, format));
                if (m_settingControls) return;
                emit previewRequested();
                if (!slider->isSliderDown()) {
                    emit commitRequested(slider->property("historyLabel").toString());
                }
            });
    connect(slider, &QSlider::sliderReleased, this,
            [this, slider]() {
                if (!m_settingControls) {
                    emit commitRequested(slider->property("historyLabel").toString());
                }
            });
    return slider;
}

void TransformPanel::setFromState(const DevelopState &state)
{
    m_settingControls = true;
    m_uprightMode = std::clamp(state.transformUprightMode, 0, 5);
    if (m_uprightButtons) {
        if (QAbstractButton *button = m_uprightButtons->button(m_uprightMode)) {
            button->setChecked(true);
        }
    }
    m_verticalSlider->setValue(std::clamp(state.transformVertical, -100, 100));
    m_horizontalSlider->setValue(std::clamp(state.transformHorizontal, -100, 100));
    m_rotateSlider->setValue(std::clamp(state.transformRotateHundredths, -1000, 1000));
    m_aspectSlider->setValue(std::clamp(state.transformAspect, -100, 100));
    m_scaleSlider->setValue(std::clamp(state.transformScale, 50, 150));
    m_xOffsetSlider->setValue(std::clamp(state.transformXOffset, -100, 100));
    m_yOffsetSlider->setValue(std::clamp(state.transformYOffset, -100, 100));
    m_constrainCrop->setChecked(state.transformConstrainCrop);
    m_settingControls = false;
}

void TransformPanel::applyToState(DevelopState &state) const
{
    state.transformUprightMode = m_uprightMode;
    state.transformVertical = m_verticalSlider->value();
    state.transformHorizontal = m_horizontalSlider->value();
    state.transformRotateHundredths = m_rotateSlider->value();
    state.transformAspect = m_aspectSlider->value();
    state.transformScale = m_scaleSlider->value();
    state.transformXOffset = m_xOffsetSlider->value();
    state.transformYOffset = m_yOffsetSlider->value();
    state.transformConstrainCrop = m_constrainCrop->isChecked();
}

bool TransformPanel::isEditing() const
{
    return m_verticalSlider->isSliderDown()
        || m_horizontalSlider->isSliderDown()
        || m_rotateSlider->isSliderDown()
        || m_aspectSlider->isSliderDown()
        || m_scaleSlider->isSliderDown()
        || m_xOffsetSlider->isSliderDown()
        || m_yOffsetSlider->isSliderDown();
}

void TransformPanel::resetAll()
{
    m_settingControls = true;
    m_uprightMode = static_cast<int>(imaging::UprightMode::Off);
    if (m_uprightButtons) {
        if (QAbstractButton *button = m_uprightButtons->button(m_uprightMode)) {
            button->setChecked(true);
        }
    }
    m_verticalSlider->setValue(0);
    m_horizontalSlider->setValue(0);
    m_rotateSlider->setValue(0);
    m_aspectSlider->setValue(0);
    m_scaleSlider->setValue(100);
    m_xOffsetSlider->setValue(0);
    m_yOffsetSlider->setValue(0);
    m_constrainCrop->setChecked(false);
    m_settingControls = false;
    emit previewRequested();
    emit commitRequested(tr("Transform Reset"));
}
