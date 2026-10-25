#include "ui/filmstrip/FilmstripWidget.h"

#include "app/ApplicationStateStore.h"
#include "ui/canvas/PhotoCanvas.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QScrollArea>
#include <QSizePolicy>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <utility>

namespace {

constexpr int kFilmstripHeight = 128;
constexpr int kThumbnailWidth = 112;
constexpr int kThumbnailImageHeight = 78;
constexpr int kThumbnailLabelHeight = 18;
constexpr int kThumbnailHeight = kThumbnailImageHeight + kThumbnailLabelHeight;
constexpr int kThumbnailPreviewSize = 320;
constexpr int kFilmstripVerticalMargin = 10;

class FilmstripThumbnail final : public QWidget
{
public:
    explicit FilmstripThumbnail(const QString &filePath,
                                const QImage &image,
                                int rating,
                                std::function<void()> activated,
                                std::function<void(int)> ratingChanged,
                                QWidget *parent = nullptr)
        : QWidget(parent)
        , m_thumbnail(image.scaled(kThumbnailPreviewSize,
                                   kThumbnailPreviewSize,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation))
        , m_fileName(QFileInfo(filePath).fileName())
        , m_activated(std::move(activated))
        , m_ratingChanged(std::move(ratingChanged))
        , m_rating(std::clamp(rating, 0, 5))
    {
        setFixedSize(kThumbnailWidth, kThumbnailHeight);
        setCursor(Qt::PointingHandCursor);
    }

    void setSelected(bool selected)
    {
        if (m_selected == selected) {
            return;
        }

        m_selected = selected;
        update();
    }

    void setRating(int rating)
    {
        const int clampedRating = std::clamp(rating, 0, 5);
        if (m_rating == clampedRating) {
            return;
        }

        m_rating = clampedRating;
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

    void contextMenuEvent(QContextMenuEvent *event) override
    {
        QMenu menu(this);
        QMenu *ratingMenu = menu.addMenu(tr("Rating"));

        for (int rating = 1; rating <= 5; ++rating) {
            QAction *action = ratingMenu->addAction(
                QString(rating, QChar(0x2605)));
            action->setCheckable(true);
            action->setChecked(m_rating == rating);
            action->setData(rating);
        }

        QAction *selectedAction = menu.exec(event->globalPos());
        if (selectedAction && m_ratingChanged) {
            bool ok = false;
            const int rating = selectedAction->data().toInt(&ok);
            if (ok && rating >= 1 && rating <= 5) {
                m_ratingChanged(rating);
            }
        }

        event->accept();
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), QColor(21, 21, 21));

        const QRect imageRect(0, 0, width(), kThumbnailImageHeight);
        painter.fillRect(imageRect, QColor(17, 17, 17));

        if (!m_thumbnail.isNull()) {
            painter.setRenderHint(QPainter::SmoothPixmapTransform, true);

            const QRect available = imageRect.adjusted(4, 4, -4, -4);
            const QSize drawnSize = m_thumbnail.size().scaled(available.size(),
                                                              Qt::KeepAspectRatio);
            const QRect target(available.center().x() - drawnSize.width() / 2,
                               available.center().y() - drawnSize.height() / 2,
                               drawnSize.width(),
                               drawnSize.height());
            painter.drawImage(target, m_thumbnail);
        }

        if (m_rating > 0) {
            QFont ratingFont = painter.font();
            ratingFont.setPointSizeF(8.5);
            ratingFont.setBold(true);
            painter.setFont(ratingFont);

            const QString stars(m_rating, QChar(0x2605));
            const int textWidth = painter.fontMetrics().horizontalAdvance(stars);
            const int textHeight = painter.fontMetrics().height();
            const QRect badgeRect(5, 5, textWidth + 7, textHeight + 2);

            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(0, 0, 0, 145));
            painter.drawRoundedRect(badgeRect, 3.0, 3.0);
            painter.setPen(QColor(238, 238, 238));
            painter.drawText(badgeRect, Qt::AlignCenter, stars);
        }

        if (m_selected) {
            painter.setPen(QPen(QColor(214, 214, 214), 2));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(imageRect.adjusted(1, 1, -2, -2));
        } else {
            painter.setPen(QColor(48, 48, 48));
            painter.setBrush(Qt::NoBrush);
            painter.drawRect(imageRect.adjusted(0, 0, -1, -1));
        }

        QFont fileNameFont = painter.font();
        fileNameFont.setPointSizeF(7.5);
        fileNameFont.setBold(false);
        painter.setFont(fileNameFont);
        painter.setPen(QColor(166, 166, 166));

        const QRect labelRect(2,
                              kThumbnailImageHeight + 1,
                              width() - 4,
                              kThumbnailLabelHeight - 1);
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
    std::function<void(int)> m_ratingChanged;
    int m_rating = 0;
    bool m_selected = false;
};

} // namespace

FilmstripWidget::FilmstripWidget(QWidget *parent)
    : QWidget(parent)
    , m_photoRatings(ApplicationStateStore::loadPhotoRatings())
{
    setObjectName(QStringLiteral("filmstripWidget"));
    setFixedHeight(kFilmstripHeight);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 1, 0, 0);
    layout->setSpacing(0);

    m_scrollArea = new QScrollArea(this);
    m_scrollArea->setObjectName(QStringLiteral("filmstripScrollArea"));
    m_scrollArea->setFrameShape(QFrame::NoFrame);
    m_scrollArea->setWidgetResizable(true);
    m_scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_scrollArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scrollArea->setFocusPolicy(Qt::NoFocus);

    m_content = new QWidget(m_scrollArea);
    m_content->setObjectName(QStringLiteral("filmstripContent"));
    m_contentLayout = new QHBoxLayout(m_content);
    m_contentLayout->setContentsMargins(0,
                                        kFilmstripVerticalMargin,
                                        0,
                                        kFilmstripVerticalMargin);
    m_contentLayout->setSpacing(8);
    m_contentLayout->addStretch(1);

    m_scrollArea->setWidget(m_content);
    layout->addWidget(m_scrollArea);

    setStyleSheet(QStringLiteral(
        "QWidget#filmstripWidget {"
        "  background: #181818;"
        "  border-top: 1px solid #2a2a2a;"
        "}"
        "QScrollArea#filmstripScrollArea, QWidget#filmstripContent {"
        "  background: #181818;"
        "  border: 0;"
        "}"
        "QScrollBar:horizontal {"
        "  background: #181818;"
        "  height: 10px;"
        "  margin: 0;"
        "}"
        "QScrollBar::handle:horizontal {"
        "  background: #3a3a3a;"
        "  min-width: 28px;"
        "  border-radius: 4px;"
        "}"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal {"
        "  width: 0;"
        "}"
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal {"
        "  background: transparent;"
        "}"));

    if (parentWidget()) {
        m_canvas = parentWidget()->findChild<PhotoCanvas *>(
            QString(), Qt::FindDirectChildrenOnly);
    }

    if (m_canvas) {
        m_canvas->installEventFilter(this);
        connect(m_canvas, &PhotoCanvas::zoomChanged, this,
                [this](qreal) {
                    alignToDefaultPhotoBounds();
                });
    }
}

void FilmstripWidget::addPhoto(const QString &filePath, const QImage &image)
{
    if (filePath.isEmpty() || image.isNull()) {
        return;
    }

    alignToDefaultPhotoBounds();

    if (m_thumbnails.contains(filePath)) {
        setCurrentPhoto(filePath);
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    auto *thumbnail = new FilmstripThumbnail(
        filePath,
        image,
        m_photoRatings.value(absoluteFilePath, 0),
        [this, filePath]() {
            activatePhoto(filePath);
        },
        [this, filePath](int rating) {
            setPhotoRating(filePath, rating);
        },
        m_content);

    m_thumbnails.insert(filePath, thumbnail);
    m_contentLayout->insertWidget(m_contentLayout->count() - 1, thumbnail);
    setCurrentPhoto(filePath);

    if (m_photoOpenedCallback) {
        m_photoOpenedCallback(filePath, image);
    }
}

void FilmstripWidget::setPhotoOpenedCallback(PhotoOpenedCallback callback)
{
    m_photoOpenedCallback = std::move(callback);
}

bool FilmstripWidget::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_canvas && event->type() == QEvent::Resize) {
        QMetaObject::invokeMethod(this,
                                  [this]() {
                                      alignToDefaultPhotoBounds(true);
                                  },
                                  Qt::QueuedConnection);
    }

    return QWidget::eventFilter(watched, event);
}

void FilmstripWidget::activatePhoto(const QString &filePath)
{
    if (filePath.isEmpty() || filePath == m_currentFilePath) {
        return;
    }

    QMetaObject::invokeMethod(window(),
                              "openImage",
                              Qt::QueuedConnection,
                              Q_ARG(QString, filePath));
}

void FilmstripWidget::setCurrentPhoto(const QString &filePath)
{
    if (filePath == m_currentFilePath) {
        return;
    }

    if (QWidget *previous = m_thumbnails.value(m_currentFilePath, nullptr)) {
        static_cast<FilmstripThumbnail *>(previous)->setSelected(false);
    }

    m_currentFilePath = filePath;

    if (QWidget *current = m_thumbnails.value(m_currentFilePath, nullptr)) {
        static_cast<FilmstripThumbnail *>(current)->setSelected(true);
    }
}

void FilmstripWidget::setPhotoRating(const QString &filePath, int rating)
{
    if (filePath.isEmpty() || rating < 1 || rating > 5) {
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    m_photoRatings.insert(absoluteFilePath, rating);
    ApplicationStateStore::setPhotoRating(absoluteFilePath, rating);

    QWidget *thumbnail = m_thumbnails.value(filePath, nullptr);
    if (!thumbnail) {
        thumbnail = m_thumbnails.value(absoluteFilePath, nullptr);
    }
    if (thumbnail) {
        static_cast<FilmstripThumbnail *>(thumbnail)->setRating(rating);
    }
}

void FilmstripWidget::alignToDefaultPhotoBounds(bool updateWhileManual)
{
    if (!m_canvas || !layout() || !m_canvas->hasImage() || m_canvas->sceneRect().isEmpty()) {
        return;
    }

    const QRect viewportRect = m_canvas->viewport()->rect();
    if (viewportRect.isEmpty()) {
        return;
    }

    const QRectF photoRect = m_canvas->sceneRect();
    const qreal fitScale = m_canvas->defaultFitScale();
    if (fitScale <= 0.0) {
        return;
    }

    const qreal currentScale = m_canvas->transform().m11();
    const bool showingDefaultFit = qFuzzyCompare(currentScale, fitScale);

    int leftInset = 0;
    int rightInset = 0;

    if (showingDefaultFit) {
        const QRect fittedRect = m_canvas->mapFromScene(photoRect).boundingRect();
        leftInset = qMax(0, fittedRect.left());
        rightInset = qMax(0, viewportRect.width() - fittedRect.right() - 1);
    } else {
        if (!updateWhileManual) {
            return;
        }

        const qreal fittedWidth = photoRect.width() * fitScale;
        const qreal centeredInset = (viewportRect.width() - fittedWidth) / 2.0;
        leftInset = qMax(0, qRound(centeredInset));
        rightInset = qMax(0, viewportRect.width()
                                - qRound(centeredInset + fittedWidth));
    }

    layout()->setContentsMargins(leftInset, 1, rightInset, 0);
}
