#pragma once

#include <QEasingCurve>
#include <QImage>
#include <QPainter>
#include <QPaintEvent>
#include <QSignalBlocker>
#include <QSizeF>
#include <QToolButton>
#include <QVariantAnimation>

#include <functional>
#include <utility>

class BeforeAfterToggle final : public QToolButton
{
public:
    using DisplayCallback = std::function<void(const QImage &, const QSizeF &)>;

    explicit BeforeAfterToggle(QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_pulseAnimation(new QVariantAnimation(this))
    {
        setObjectName(QStringLiteral("beforeAfterToggle"));
        setText(QStringLiteral("Before / After"));
        setCheckable(true);
        setEnabled(false);
        setFixedSize(104, 24);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(tr("Toggle original and current edit"));

        m_pulseAnimation->setDuration(180);
        m_pulseAnimation->setStartValue(0.0);
        m_pulseAnimation->setKeyValueAt(0.5, 1.0);
        m_pulseAnimation->setEndValue(0.0);
        m_pulseAnimation->setEasingCurve(QEasingCurve::InOutQuad);

        connect(m_pulseAnimation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_pulse = value.toReal();
                    update();
                });

        connect(this, &QToolButton::clicked, this,
                [this]() {
                    m_pulseAnimation->stop();
                    m_pulseAnimation->start();
                });

        connect(this, &QToolButton::toggled, this,
                [this](bool showingBefore) {
                    if (!m_displayCallback) {
                        return;
                    }

                    if (showingBefore) {
                        if (!m_originalImage.isNull()) {
                            m_displayCallback(m_originalImage,
                                              QSizeF(m_originalImage.size()));
                        }
                        return;
                    }

                    if (!m_currentImage.isNull()) {
                        m_displayCallback(m_currentImage, m_currentLogicalSize);
                    }
                });
    }

    void setDisplayCallback(DisplayCallback callback)
    {
        m_displayCallback = std::move(callback);
    }

    void setOriginalImage(const QImage &image)
    {
        m_originalImage = image;
        m_currentImage = image;
        m_currentLogicalSize = QSizeF(image.size());

        const QSignalBlocker blocker(this);
        setChecked(false);
        setEnabled(!image.isNull());
    }

    void setCurrentImage(const QImage &image, const QSizeF &logicalSize)
    {
        if (image.isNull()) {
            return;
        }

        m_currentImage = image;
        m_currentLogicalSize = logicalSize.isValid() && !logicalSize.isEmpty()
            ? logicalSize
            : QSizeF(image.size());
    }

    void clearImages()
    {
        m_originalImage = {};
        m_currentImage = {};
        m_currentLogicalSize = {};

        const QSignalBlocker blocker(this);
        setChecked(false);
        setEnabled(false);
    }

    [[nodiscard]] bool showingBefore() const noexcept
    {
        return isChecked();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QPointF center = QRectF(rect()).center();
        const qreal pulseScale = 1.0 + 0.08 * m_pulse;
        painter.translate(center);
        painter.scale(pulseScale, pulseScale);
        painter.translate(-center);

        QFont textFont = font();
        textFont.setBold(true);
        painter.setFont(textFont);

        const QColor textColor = !isEnabled()
            ? QColor(92, 92, 92)
            : (underMouse() ? QColor(255, 255, 255) : QColor(205, 205, 205));
        painter.setPen(textColor);
        painter.drawText(rect(), Qt::AlignCenter, text());
    }

private:
    QImage m_originalImage;
    QImage m_currentImage;
    QSizeF m_currentLogicalSize;
    DisplayCallback m_displayCallback;
    QVariantAnimation *m_pulseAnimation = nullptr;
    qreal m_pulse = 0.0;
};
