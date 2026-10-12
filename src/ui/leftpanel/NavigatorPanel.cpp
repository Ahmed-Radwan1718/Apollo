#include "ui/leftpanel/NavigatorPanel.h"

#include "ui/canvas/PhotoCanvas.h"
#include "ui/sidebar/SidebarCollapseHandle.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <cmath>

namespace {

constexpr int kDynamicZoomRole = Qt::UserRole + 1;

} // namespace

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
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(8);

    auto *header = new QWidget(this);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(0, 0, 0, 0);
    headerLayout->setSpacing(6);

    auto *title = new QLabel(tr("Navigator"), header);
    title->setObjectName(QStringLiteral("navigatorTitle"));
    headerLayout->addWidget(title);
    headerLayout->addStretch(1);

    m_zoomCombo = new QComboBox(header);
    m_zoomCombo->setObjectName(QStringLiteral("navigatorZoomCombo"));
    m_zoomCombo->setFixedSize(76, 22);
    m_zoomCombo->setFocusPolicy(Qt::NoFocus);
    m_zoomCombo->setCursor(Qt::PointingHandCursor);
    m_zoomCombo->setMaxVisibleItems(8);
    m_zoomCombo->addItem(tr("Fit"), -1.0);
    m_zoomCombo->addItem(QStringLiteral("25%"), 25.0);
    m_zoomCombo->addItem(QStringLiteral("50%"), 50.0);
    m_zoomCombo->addItem(QStringLiteral("200%"), 200.0);
    m_zoomCombo->addItem(QStringLiteral("400%"), 400.0);
    m_zoomCombo->addItem(QStringLiteral("800%"), 800.0);
    m_zoomCombo->addItem(QStringLiteral("1600%"), 1600.0);
    m_zoomCombo->setEnabled(false);
    headerLayout->addWidget(m_zoomCombo);

    layout->addWidget(header);

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
        "}"
        "QComboBox#navigatorZoomCombo {"
        "  background: transparent;"
        "  color: #bdbdbd;"
        "  border: 0;"
        "  padding: 1px 18px 1px 5px;"
        "  font-size: 11px;"
        "}"
        "QComboBox#navigatorZoomCombo:hover {"
        "  color: #f2f2f2;"
        "  background: #222222;"
        "  border-radius: 3px;"
        "}"
        "QComboBox#navigatorZoomCombo:disabled {"
        "  color: #686868;"
        "}"
        "QComboBox#navigatorZoomCombo::drop-down {"
        "  border: 0;"
        "  width: 18px;"
        "}"
        "QComboBox#navigatorZoomCombo QAbstractItemView {"
        "  background: #202020;"
        "  color: #d0d0d0;"
        "  border: 1px solid #343434;"
        "  outline: 0;"
        "  selection-background-color: #303030;"
        "  selection-color: #ffffff;"
        "}"));

    connect(m_zoomCombo, &QComboBox::activated, this,
            [this](int index) {
                if (!m_canvas || index < 0) {
                    return;
                }

                const qreal zoom = m_zoomCombo->itemData(index).toDouble();
                if (zoom < 0.0) {
                    m_canvas->fitToView();
                } else {
                    m_canvas->setRelativeZoomPercent(zoom);
                }
            });

    if (QWidget *sidePanel = parentWidget()) {
        new SidebarCollapseHandle(sidePanel,
                                  SidebarCollapseHandle::Side::Left);
    }
}

void NavigatorPanel::setImage(const QImage &image)
{
    m_preview->setImage(image);
    m_zoomCombo->setEnabled(!image.isNull());
}

void NavigatorPanel::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    bindCanvas();
}

void NavigatorPanel::bindCanvas()
{
    if (m_canvas) {
        return;
    }

    QWidget *root = window();
    if (!root) {
        return;
    }

    m_canvas = root->findChild<PhotoCanvas *>();
    if (!m_canvas) {
        return;
    }

    connect(m_canvas, &PhotoCanvas::zoomChanged, this,
            [this](qreal) {
                if (m_canvas) {
                    setZoomState(m_canvas->relativeZoomPercent(),
                                 m_canvas->isFitZoom());
                }
            });

    setZoomState(m_canvas->relativeZoomPercent(), m_canvas->isFitZoom());
}

void NavigatorPanel::setZoomState(qreal percent, bool fitMode)
{
    if (!m_zoomCombo) {
        return;
    }

    const QSignalBlocker blocker(m_zoomCombo);

    for (int index = m_zoomCombo->count() - 1; index >= 0; --index) {
        if (m_zoomCombo->itemData(index, kDynamicZoomRole).toBool()) {
            m_zoomCombo->removeItem(index);
        }
    }

    if (fitMode || std::abs(percent - 100.0) < 0.5) {
        m_zoomCombo->setCurrentIndex(m_zoomCombo->findData(-1.0));
        return;
    }

    for (int index = 0; index < m_zoomCombo->count(); ++index) {
        const qreal preset = m_zoomCombo->itemData(index).toDouble();
        if (preset > 0.0 && std::abs(preset - percent) < 0.5) {
            m_zoomCombo->setCurrentIndex(index);
            return;
        }
    }

    const int roundedPercent = qMax(1, qRound(percent));
    m_zoomCombo->insertItem(1,
                            QStringLiteral("%1%").arg(roundedPercent),
                            percent);
    m_zoomCombo->setItemData(1, true, kDynamicZoomRole);
    m_zoomCombo->setCurrentIndex(1);
}
