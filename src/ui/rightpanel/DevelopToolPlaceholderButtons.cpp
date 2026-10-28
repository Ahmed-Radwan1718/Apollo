#include "ui/rightpanel/DevelopToolPlaceholderButtons.h"

#include <QColor>
#include <QHBoxLayout>
#include <QImage>
#include <QObject>
#include <QPainter>
#include <QRectF>
#include <QString>
#include <QSvgRenderer>
#include <QToolButton>
#include <QWidget>

namespace {

constexpr qreal kIconSize = 18.0;
constexpr qreal kIconVerticalOffset = 4.5;

class SvgPlaceholderButton final : public QToolButton
{
public:
    SvgPlaceholderButton(const QString &resourcePath,
                         const QString &toolTip,
                         QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_renderer(resourcePath)
    {
        setFixedSize(34, 30);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setToolTip(toolTip);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        if (!m_renderer.isValid()) {
            return;
        }

        const QColor iconColor = !isEnabled()
            ? QColor(86, 86, 86)
            : (underMouse() ? QColor(244, 244, 244)
                            : QColor(190, 190, 190));

        const qreal devicePixelRatio = devicePixelRatioF();
        const QSize pixelSize(qMax(1, qRound(kIconSize * devicePixelRatio)),
                              qMax(1, qRound(kIconSize * devicePixelRatio)));
        QImage icon(pixelSize, QImage::Format_ARGB32_Premultiplied);
        icon.setDevicePixelRatio(devicePixelRatio);
        icon.fill(Qt::transparent);

        const QRectF iconRect(0.0, 0.0, kIconSize, kIconSize);
        {
            QPainter svgPainter(&icon);
            svgPainter.setRenderHint(QPainter::Antialiasing, true);
            m_renderer.render(&svgPainter, iconRect);
        }
        {
            QPainter tintPainter(&icon);
            tintPainter.setCompositionMode(QPainter::CompositionMode_SourceIn);
            tintPainter.fillRect(iconRect, iconColor);
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        const QPointF center = QRectF(rect()).center()
            + QPointF(0.0, kIconVerticalOffset);
        painter.drawImage(center - QPointF(kIconSize * 0.5, kIconSize * 0.5), icon);
    }

private:
    QSvgRenderer m_renderer;
};

} // namespace

void installDevelopToolPlaceholderButtons(QWidget *root)
{
    if (!root) {
        return;
    }

    auto *toolStrip = root->findChild<QWidget *>(QStringLiteral("developToolStrip"));
    auto *cropButton = toolStrip
        ? toolStrip->findChild<QToolButton *>(QStringLiteral("cropToolSelector"))
        : nullptr;
    auto *layout = toolStrip
        ? qobject_cast<QHBoxLayout *>(toolStrip->layout())
        : nullptr;
    if (!toolStrip || !cropButton || !layout) {
        return;
    }

    const int cropIndex = layout->indexOf(cropButton);
    if (cropIndex < 0) {
        return;
    }

    auto *maskButton = new SvgPlaceholderButton(
        QStringLiteral(":/resources/icons/mask.svg"),
        QObject::tr("Mask"),
        toolStrip);
    maskButton->setObjectName(QStringLiteral("maskToolSelector"));

    auto *eraserButton = new SvgPlaceholderButton(
        QStringLiteral(":/resources/icons/eraser.svg"),
        QObject::tr("Eraser"),
        toolStrip);
    eraserButton->setObjectName(QStringLiteral("eraserToolSelector"));

    layout->insertWidget(cropIndex + 1, maskButton);
    layout->insertWidget(cropIndex + 2, eraserButton);
}
