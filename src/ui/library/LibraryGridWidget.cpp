#include "ui/library/LibraryGridWidget.h"

#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QGridLayout>
#include <QImageReader>
#include <QLabel>
#include <QLayoutItem>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QStackedWidget>
#include <QUrl>
#include <QVBoxLayout>

#include <algorithm>
#include <utility>

namespace {

constexpr int kThumbnailWidth = 176;
constexpr int kThumbnailImageHeight = 112;
constexpr int kThumbnailLabelHeight = 24;
constexpr int kThumbnailHeight = kThumbnailImageHeight + kThumbnailLabelHeight;
constexpr int kThumbnailPreviewSize = 640;
constexpr int kGridMargin = 16;
constexpr int kGridSpacing = 12;

bool isSupportedImageFile(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.isFile()) {
        return false;
    }

    const QByteArray suffix = fileInfo.suffix().toLatin1().toLower();
    if (suffix == "arw") {
        return true;
    }

    return QImageReader::supportedImageFormats().contains(suffix);
}

QStringList supportedLocalFiles(const QMimeData *mimeData)
{
    QStringList filePaths;
    if (!mimeData || !mimeData->hasUrls()) {
        return filePaths;
    }

    for (const QUrl &url : mimeData->urls()) {
        if (!url.isLocalFile()) {
            continue;
        }

        const QString filePath = QFileInfo(url.toLocalFile()).absoluteFilePath();
        if (isSupportedImageFile(filePath) && !filePaths.contains(filePath)) {
            filePaths.push_back(filePath);
        }
    }

    return filePaths;
}

class LibraryThumbnail final : public QWidget
{
public:
    LibraryThumbnail(const QString &filePath,
                     const QImage &image,
                     std::function<void()> activated,
                     QWidget *parent = nullptr)
        : QWidget(parent)
        , m_fileName(QFileInfo(filePath).fileName())
        , m_activated(std::move(activated))
    {
        setFixedSize(kThumbnailWidth, kThumbnailHeight);
        setCursor(Qt::PointingHandCursor);
        setImage(image);
    }

    void setImage(const QImage &image)
    {
        m_thumbnail = image.scaled(kThumbnailPreviewSize,
                                   kThumbnailPreviewSize,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation);
        update();
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected) {
            return;
        }

        m_selected = selected;
        update();
    }

protected:
    void mouseReleaseEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton
            && rect().contains(event->position().toPoint())
            && m_activated) {
            m_activated();
        }

        QWidget::mouseReleaseEvent(event);
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(24, 24, 24));

        const QRect imageRect(0, 0, width(), kThumbnailImageHeight);
        painter.fillRect(imageRect, QColor(17, 17, 17));

        if (!m_thumbnail.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            const QRect available = imageRect.adjusted(5, 5, -5, -5);
            const QSize drawnSize = m_thumbnail.size().scaled(available.size(),
                                                              Qt::KeepAspectRatio);
            const QRect target(available.center().x() - drawnSize.width() / 2,
                               available.center().y() - drawnSize.height() / 2,
                               drawnSize.width(),
                               drawnSize.height());
            painter.drawImage(target, m_thumbnail);
        }

        if (m_selected) {
            painter.setPen(QPen(QColor(214, 214, 214), 2));
            painter.drawRect(imageRect.adjusted(1, 1, -2, -2));
        } else {
            painter.setPen(QColor(48, 48, 48));
            painter.drawRect(imageRect.adjusted(0, 0, -1, -1));
        }

        QFont fileNameFont = painter.font();
        fileNameFont.setPointSizeF(8.0);
        painter.setFont(fileNameFont);
        painter.setPen(QColor(181, 181, 181));

        const QRect labelRect(4,
                              kThumbnailImageHeight,
                              width() - 8,
                              kThumbnailLabelHeight);
        const QString displayName = painter.fontMetrics().elidedText(
            m_fileName, Qt::ElideMiddle, labelRect.width());
        painter.drawText(labelRect,
                         Qt::AlignHCenter | Qt::AlignVCenter,
                         displayName);
    }

private:
    QImage m_thumbnail;
    QString m_fileName;
    std::function<void()> m_activated;
    bool m_selected = false;
};

} // namespace

LibraryGridWidget::LibraryGridWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("libraryGridWidget"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAcceptDrops(true);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_stack = new QStackedWidget(this);
    m_stack->setObjectName(QStringLiteral("libraryGridStack"));
    m_stack->setAcceptDrops(false);

    auto *emptyLabel = new QLabel(tr("No photos in Library"), m_stack);
    emptyLabel->setObjectName(QStringLiteral("libraryEmptyLabel"));
    emptyLabel->setAlignment(Qt::AlignCenter);
    emptyLabel->setAcceptDrops(false);
    m_stack->addWidget(emptyLabel);

    m_scrollArea = new QScrollArea(m_stack);
    m_scrollArea->setObjectName(QStringLiteral("libraryGridScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFocusPolicy(Qt::NoFocus);
    m_scrollArea->setAcceptDrops(false);
    m_scrollArea->viewport()->setAcceptDrops(false);

    m_content = new QWidget(m_scrollArea);
    m_content->setObjectName(QStringLiteral("libraryGridContent"));
    m_content->setAcceptDrops(false);
    m_gridLayout = new QGridLayout(m_content);
    m_gridLayout->setContentsMargins(kGridMargin,
                                     kGridMargin,
                                     kGridMargin,
                                     kGridMargin);
    m_gridLayout->setHorizontalSpacing(kGridSpacing);
    m_gridLayout->setVerticalSpacing(kGridSpacing);
    m_gridLayout->setAlignment(Qt::AlignTop | Qt::AlignLeft);

    m_scrollArea->setWidget(m_content);
    m_stack->addWidget(m_scrollArea);
    layout->addWidget(m_stack, 1);

    setStyleSheet(QStringLiteral(
        "QWidget#libraryGridWidget, QWidget#libraryGridContent,"
        "QStackedWidget#libraryGridStack, QScrollArea#libraryGridScrollArea {"
        "  background: #181818;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#libraryEmptyLabel {"
        "  background: #181818;"
        "  color: #777777;"
        "  font-family: \"Segoe UI Variable\";"
        "  font-size: 12px;"
        "}"
        "QScrollBar:vertical {"
        "  background: #181818;"
        "  width: 10px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:vertical {"
        "  background: #3a3a3a;"
        "  min-height: 28px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical {"
        "  height: 0;"
        "}"
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical {"
        "  background: transparent;"
        "}"));
}

void LibraryGridWidget::addPhoto(const QString &filePath, const QImage &image)
{
    if (filePath.isEmpty() || image.isNull()) {
        return;
    }

    if (QWidget *existing = m_thumbnails.value(filePath, nullptr)) {
        static_cast<LibraryThumbnail *>(existing)->setImage(image);
        setCurrentPhoto(filePath);
        return;
    }

    auto *thumbnail = new LibraryThumbnail(
        filePath,
        image,
        [this, filePath]() {
            setCurrentPhoto(filePath);
            if (m_photoActivated) {
                m_photoActivated(filePath);
            }
        },
        m_content);

    m_thumbnails.insert(filePath, thumbnail);
    m_order.push_back(filePath);
    m_stack->setCurrentWidget(m_scrollArea);
    setCurrentPhoto(filePath);
    reflow();
}

void LibraryGridWidget::setPhotoActivatedCallback(
    std::function<void(const QString &)> callback)
{
    m_photoActivated = std::move(callback);
}

void LibraryGridWidget::setFilesDroppedCallback(FilesDroppedCallback callback)
{
    m_filesDropped = std::move(callback);
}

void LibraryGridWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    reflow();
}

void LibraryGridWidget::dragEnterEvent(QDragEnterEvent *event)
{
    if (!supportedLocalFiles(event->mimeData()).isEmpty()) {
        event->acceptProposedAction();
        return;
    }

    event->ignore();
}

void LibraryGridWidget::dropEvent(QDropEvent *event)
{
    const QStringList filePaths = supportedLocalFiles(event->mimeData());
    if (filePaths.isEmpty()) {
        event->ignore();
        return;
    }

    if (m_filesDropped) {
        m_filesDropped(filePaths);
    }

    event->acceptProposedAction();
}

void LibraryGridWidget::reflow()
{
    if (!m_gridLayout || !m_scrollArea || m_order.isEmpty()) {
        return;
    }

    while (QLayoutItem *item = m_gridLayout->takeAt(0)) {
        delete item;
    }

    const int availableWidth = std::max(
        1,
        m_scrollArea->viewport()->width() - (kGridMargin * 2));
    const int columns = std::max(
        1,
        (availableWidth + kGridSpacing) / (kThumbnailWidth + kGridSpacing));

    for (int index = 0; index < m_order.size(); ++index) {
        QWidget *thumbnail = m_thumbnails.value(m_order.at(index), nullptr);
        if (!thumbnail) {
            continue;
        }

        m_gridLayout->addWidget(thumbnail,
                                index / columns,
                                index % columns,
                                Qt::AlignTop | Qt::AlignLeft);
    }
}

void LibraryGridWidget::setCurrentPhoto(const QString &filePath)
{
    if (filePath == m_currentFilePath) {
        return;
    }

    if (QWidget *previous = m_thumbnails.value(m_currentFilePath, nullptr)) {
        static_cast<LibraryThumbnail *>(previous)->setSelected(false);
    }

    m_currentFilePath = filePath;

    if (QWidget *current = m_thumbnails.value(m_currentFilePath, nullptr)) {
        static_cast<LibraryThumbnail *>(current)->setSelected(true);
    }
}

void LibraryGridWidget::updateHeader()
{
    if (!m_countLabel) {
        return;
    }

    const int count = m_order.size();
    m_countLabel->setText(count == 1
                              ? tr("1 Photo")
                              : tr("%1 Photos").arg(count));
}
