#include "ui/leftpanel/NavigatorPanel.h"

#include "ui/sidebar/SidebarCollapseHandle.h"

#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QVBoxLayout>

class NavigatorPanel::PreviewWidget final : public QWidget
{
public:
    explicit PreviewWidget(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setMinimumHeight(145);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

    QSize sizeHint() const override
    {
        return QSize(220, 150);
    }

    void setImage(const QImage &image)
    {
        if (image.isNull()) {
            m_preview = {};
            update();
            return;
        }

        // Keep only a small display copy so painting the Navigator never scales
        // the full-resolution RAW render on every repaint.
        m_preview = image.scaled(640, 640,
                                 Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
        update();
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(20, 20, 20));

        if (m_preview.isNull()) {
            return;
        }

        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

        const QSizeF imageSize = m_preview.size();
        const QSizeF availableSize = size();
        const qreal scale = qMin(availableSize.width() / imageSize.width(),
                                 availableSize.height() / imageSize.height());
        const QSizeF drawnSize = imageSize * scale;
        const QRectF target((width() - drawnSize.width()) / 2.0,
                            (height() - drawnSize.height()) / 2.0,
                            drawnSize.width(),
                            drawnSize.height());

        painter.drawImage(target, m_preview);
    }

private:
    QImage m_preview;
};

NavigatorPanel::NavigatorPanel(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("navigatorPanel"));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(8);

    auto *title = new QLabel(tr("Navigator"), this);
    title->setObjectName(QStringLiteral("navigatorTitle"));
    layout->addWidget(title);

    m_preview = new PreviewWidget(this);
    layout->addWidget(m_preview);

    setStyleSheet(QStringLiteral(
        "QWidget#navigatorPanel {"
        "  background: #181818;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#navigatorTitle {"
        "  color: #d6d6d6;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0;"
        "}"));

    if (QWidget *sidePanel = parentWidget()) {
        new SidebarCollapseHandle(sidePanel,
                                  SidebarCollapseHandle::Side::Left);
    }
}

void NavigatorPanel::setImage(const QImage &image)
{
    m_preview->setImage(image);
}
