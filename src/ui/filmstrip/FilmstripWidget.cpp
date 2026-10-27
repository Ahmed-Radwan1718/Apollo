#include "ui/filmstrip/FilmstripWidget.h"

#include "app/ApplicationStateStore.h"
#include "ui/canvas/PhotoCanvas.h"

#include <QAction>
#include <QContextMenuEvent>
#include <QDir>
#include <QEvent>
#include <QFileInfo>
#include <QFont>
#include <QFrame>
#include <QHBoxLayout>
#include <QIcon>
#include <QMenu>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QPaintEvent>
#include <QPixmap>
#include <QProcess>
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
constexpr int kContextMenuWidth = 220;
constexpr int kContextSubmenuWidth = 210;
constexpr int kColorLabelSubmenuWidth = 176;

void configureContextMenu(QMenu *menu, int minimumWidth)
{
    if (!menu) {
        return;
    }

    menu->setMinimumWidth(minimumWidth);
    menu->setStyleSheet(QStringLiteral(
        "QMenu::item {"
        "  padding: 7px 34px 7px 14px;"
        "}"
        "QMenu::item:selected {"
        "  background: rgba(255, 255, 255, 26);"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  margin: 5px 10px;"
        "}"));
}

void configureColorLabelMenu(QMenu *menu)
{
    if (!menu) {
        return;
    }

    menu->setMinimumWidth(kColorLabelSubmenuWidth);
    menu->setStyleSheet(QStringLiteral(
        "QMenu::item {"
        "  padding: 7px 16px 7px 20px;"
        "}"
        "QMenu::item:selected {"
        "  background: rgba(255, 255, 255, 26);"
        "}"
        "QMenu::icon {"
        "  position: relative;"
        "  left: 18px;"
        "}"
        "QMenu::separator {"
        "  height: 1px;"
        "  margin: 5px 10px;"
        "}"));
}

void revealInFileExplorer(const QString &filePath)
{
    const QFileInfo fileInfo(filePath);
    if (!fileInfo.exists()) {
        return;
    }

    QString explorerPath;
    const QString windowsDirectory = qEnvironmentVariable("WINDIR");
    if (!windowsDirectory.isEmpty()) {
        explorerPath = QDir(windowsDirectory).filePath(QStringLiteral("explorer.exe"));
    }
    if (explorerPath.isEmpty() || !QFileInfo::exists(explorerPath)) {
        explorerPath = QStringLiteral("explorer.exe");
    }

    QProcess explorer;
    explorer.setProgram(explorerPath);
    explorer.setNativeArguments(
        QStringLiteral("/select,\"%1\"")
            .arg(QDir::toNativeSeparators(fileInfo.absoluteFilePath())));
    explorer.startDetached();
}

QColor colorForLabel(int colorLabel)
{
    switch (colorLabel) {
    case 1:
        return QColor(QStringLiteral("#D75A5A"));
    case 2:
        return QColor(QStringLiteral("#D6B84F"));
    case 3:
        return QColor(QStringLiteral("#5AA66A"));
    case 4:
        return QColor(QStringLiteral("#5B84C4"));
    case 5:
        return QColor(QStringLiteral("#9B6BC4"));
    default:
        return {};
    }
}

QIcon colorLabelIcon(int colorLabel)
{
    constexpr int iconSize = 14;
    constexpr qreal circleInset = 2.0;

    QPixmap pixmap(iconSize, iconSize);
    pixmap.fill(Qt::transparent);

    const QColor color = colorLabel == 0
        ? QColor(128, 128, 128, 150)
        : colorForLabel(colorLabel);
    if (color.isValid()) {
        QPainter painter(&pixmap);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawEllipse(QRectF(circleInset,
                                   circleInset,
                                   iconSize - circleInset * 2.0,
                                   iconSize - circleInset * 2.0));
    }

    return QIcon(pixmap);
}

QIcon statusIcon(int status)
{
    constexpr int iconSize = 14;

    QPixmap pixmap(iconSize, iconSize);
    pixmap.fill(Qt::transparent);

    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (status == 1) {
        const QColor color(QStringLiteral("#5AA66A"));
        painter.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(QPointF(3.5, 2.0), QPointF(3.5, 12.0));

        const QPointF flag[] = {
            QPointF(4.0, 2.5),
            QPointF(11.0, 4.5),
            QPointF(4.0, 6.5)
        };
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        painter.drawPolygon(flag, 3);
    } else if (status == -1) {
        painter.setPen(QPen(QColor(QStringLiteral("#D75A5A")),
                            2.0,
                            Qt::SolidLine,
                            Qt::RoundCap));
        painter.drawLine(QPointF(3.5, 3.5), QPointF(10.5, 10.5));
        painter.drawLine(QPointF(10.5, 3.5), QPointF(3.5, 10.5));
    }

    return QIcon(pixmap);
}

class FilmstripThumbnail final : public QWidget
{
public:
    explicit FilmstripThumbnail(const QString &filePath,
                                const QImage &image,
                                int rating,
                                int status,
                                int colorLabel,
                                std::function<void()> activated,
                                std::function<void(int)> ratingChanged,
                                std::function<void(int)> statusChanged,
                                std::function<void(int)> colorLabelChanged,
                                std::function<void()> removed,
                                QWidget *parent = nullptr)
        : QWidget(parent)
        , m_thumbnail(image.scaled(kThumbnailPreviewSize,
                                   kThumbnailPreviewSize,
                                   Qt::KeepAspectRatio,
                                   Qt::SmoothTransformation))
        , m_filePath(QFileInfo(filePath).absoluteFilePath())
        , m_fileName(QFileInfo(filePath).fileName())
        , m_activated(std::move(activated))
        , m_ratingChanged(std::move(ratingChanged))
        , m_statusChanged(std::move(statusChanged))
        , m_colorLabelChanged(std::move(colorLabelChanged))
        , m_removed(std::move(removed))
        , m_rating(std::clamp(rating, 0, 5))
        , m_status(status == 1 || status == -1 ? status : 0)
        , m_colorLabel(std::clamp(colorLabel, 0, 5))
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

    void setStatus(int status)
    {
        const int normalizedStatus = status == 1 || status == -1 ? status : 0;
        if (m_status == normalizedStatus) {
            return;
        }

        m_status = normalizedStatus;
    }

    void setColorLabel(int colorLabel)
    {
        const int clampedColorLabel = std::clamp(colorLabel, 0, 5);
        if (m_colorLabel == clampedColorLabel) {
            return;
        }

        m_colorLabel = clampedColorLabel;
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
        configureContextMenu(&menu, kContextMenuWidth);

        QMenu *flagMenu = menu.addMenu(tr("Set Flag"));
        configureColorLabelMenu(flagMenu);
        QHash<QAction *, int> flagActions;
        const auto addFlagAction = [&](const QString &text, int status) {
            QAction *action = flagMenu->addAction(text);
            action->setIcon(statusIcon(status));
            action->setIconVisibleInMenu(true);
            action->setCheckable(true);
            action->setChecked(m_status == status);
            flagActions.insert(action, status);
        };
        addFlagAction(tr("Accept"), 1);
        addFlagAction(tr("Rejected"), -1);

        QMenu *ratingMenu = menu.addMenu(tr("Set Rating"));
        configureContextMenu(ratingMenu, kContextSubmenuWidth);
        QHash<QAction *, int> ratingActions;
        const auto addRatingAction = [&](const QString &text, int rating) {
            QAction *action = ratingMenu->addAction(text);
            action->setCheckable(true);
            action->setChecked(m_rating == rating);
            ratingActions.insert(action, rating);
        };

        addRatingAction(tr("None"), 0);
        ratingMenu->addSeparator();
        for (int rating = 1; rating <= 5; ++rating) {
            const QString text = rating == 1
                ? tr("1 Star")
                : tr("%1 Stars").arg(rating);
            addRatingAction(text, rating);
        }

        QMenu *colorLabelMenu = menu.addMenu(tr("Set Color Label"));
        configureColorLabelMenu(colorLabelMenu);
        QHash<QAction *, int> colorActions;
        const auto addColorAction = [&](const QString &text, int colorLabel) {
            QAction *action = colorLabelMenu->addAction(text);
            action->setIcon(colorLabelIcon(colorLabel));
            action->setCheckable(true);
            action->setChecked(m_colorLabel == colorLabel);
            colorActions.insert(action, colorLabel);
        };

        addColorAction(tr("None"), 0);
        colorLabelMenu->addSeparator();
        addColorAction(tr("Red"), 1);
        addColorAction(tr("Yellow"), 2);
        addColorAction(tr("Green"), 3);
        addColorAction(tr("Blue"), 4);
        addColorAction(tr("Purple"), 5);

        menu.addSeparator();
        QAction *showInExplorerAction = menu.addAction(tr("Show in File Explorer"));
        menu.addSeparator();
        QAction *removeFromApolloAction = menu.addAction(tr("Remove from Apollo"));

        QAction *selectedAction = menu.exec(event->globalPos());
        if (selectedAction && flagActions.contains(selectedAction)) {
            if (m_statusChanged) {
                m_statusChanged(flagActions.value(selectedAction));
            }
        } else if (selectedAction && ratingActions.contains(selectedAction)) {
            if (m_ratingChanged) {
                m_ratingChanged(ratingActions.value(selectedAction));
            }
        } else if (selectedAction && colorActions.contains(selectedAction)) {
            if (m_colorLabelChanged) {
                m_colorLabelChanged(colorActions.value(selectedAction));
            }
        } else if (selectedAction == showInExplorerAction) {
            revealInFileExplorer(m_filePath);
        } else if (selectedAction == removeFromApolloAction && m_removed) {
            m_removed();
        }

        event->accept();
    }

    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.fillRect(rect(), m_selected ? QColor(36, 36, 36)
                                            : QColor(21, 21, 21));

        const QRect imageRect(0, 0, width(), kThumbnailImageHeight);
        painter.fillRect(imageRect, m_selected ? QColor(31, 31, 31)
                                               : QColor(17, 17, 17));

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
            QFont ratingFont(QStringLiteral("Segoe UI Symbol"));
            ratingFont.setPointSizeF(8.5);
            ratingFont.setWeight(QFont::Normal);
            ratingFont.setLetterSpacing(QFont::AbsoluteSpacing, 1.0);
            painter.setFont(ratingFont);
            painter.setRenderHint(QPainter::TextAntialiasing, true);
            painter.setPen(QColor(QStringLiteral("#C09A5C")));

            const QString stars(m_rating, QChar(0x2605));
            const int textWidth = painter.fontMetrics().horizontalAdvance(stars);
            const int textHeight = painter.fontMetrics().height();
            const QRect ratingRect(5,
                                   imageRect.bottom() - textHeight - 3,
                                   textWidth,
                                   textHeight);
            painter.drawText(ratingRect,
                             Qt::AlignLeft | Qt::AlignVCenter,
                             stars);
        }

        const QColor labelColor = colorForLabel(m_colorLabel);
        painter.setBrush(Qt::NoBrush);
        if (labelColor.isValid()) {
            painter.setPen(QPen(labelColor, 2));
            painter.drawRect(imageRect.adjusted(1, 1, -2, -2));
        } else {
            painter.setPen(QColor(48, 48, 48));
            painter.drawRect(imageRect.adjusted(0, 0, -1, -1));
        }

        QFont fileNameFont = font();
        fileNameFont.setPointSizeF(7.5);
        fileNameFont.setBold(false);
        painter.setFont(fileNameFont);
        painter.setPen(m_selected ? QColor(214, 214, 214)
                                  : QColor(166, 166, 166));

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
    QString m_filePath;
    QString m_fileName;
    std::function<void()> m_activated;
    std::function<void(int)> m_ratingChanged;
    std::function<void(int)> m_statusChanged;
    std::function<void(int)> m_colorLabelChanged;
    std::function<void()> m_removed;
    int m_rating = 0;
    int m_status = 0;
    int m_colorLabel = 0;
    bool m_selected = false;
};

} // namespace

FilmstripWidget::FilmstripWidget(QWidget *parent)
    : QWidget(parent)
    , m_photoRatings(ApplicationStateStore::loadPhotoRatings())
    , m_photoStatuses(ApplicationStateStore::loadPhotoStatuses())
    , m_photoColorLabels(ApplicationStateStore::loadPhotoColorLabels())
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
        m_photoStatuses.value(absoluteFilePath, 0),
        m_photoColorLabels.value(absoluteFilePath, 0),
        [this, filePath]() {
            activatePhoto(filePath);
        },
        [this, filePath](int rating) {
            setPhotoRating(filePath, rating);
        },
        [this, filePath](int status) {
            setPhotoStatus(filePath, status);
        },
        [this, filePath](int colorLabel) {
            setPhotoColorLabel(filePath, colorLabel);
        },
        [this, filePath]() {
            removePhoto(filePath);
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

void FilmstripWidget::setPhotoRemovedCallback(PhotoRemovedCallback callback)
{
    m_photoRemovedCallback = std::move(callback);
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

void FilmstripWidget::removePhoto(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    QString storedPath = filePath;
    QWidget *thumbnail = m_thumbnails.value(storedPath, nullptr);
    if (!thumbnail) {
        for (auto iterator = m_thumbnails.constBegin(); iterator != m_thumbnails.constEnd(); ++iterator) {
            if (QFileInfo(iterator.key()).absoluteFilePath() == absoluteFilePath) {
                storedPath = iterator.key();
                thumbnail = iterator.value();
                break;
            }
        }
    }
    if (!thumbnail) {
        return;
    }

    QString nextPath;
    if (m_contentLayout) {
        const int index = m_contentLayout->indexOf(thumbnail);
        if (index >= 0) {
            if (QLayoutItem *nextItem = m_contentLayout->itemAt(index + 1)) {
                if (QWidget *nextWidget = nextItem->widget()) {
                    nextPath = m_thumbnails.key(nextWidget);
                }
            }
            if (nextPath.isEmpty() && index > 0) {
                if (QLayoutItem *previousItem = m_contentLayout->itemAt(index - 1)) {
                    if (QWidget *previousWidget = previousItem->widget()) {
                        nextPath = m_thumbnails.key(previousWidget);
                    }
                }
            }
        }
    }

    const bool removingCurrent =
        m_currentFilePath == storedPath
        || (!m_currentFilePath.isEmpty()
            && QFileInfo(m_currentFilePath).absoluteFilePath() == absoluteFilePath);
    if (removingCurrent) {
        m_currentFilePath.clear();
    }

    m_thumbnails.remove(storedPath);
    if (m_contentLayout) {
        m_contentLayout->removeWidget(thumbnail);
    }
    thumbnail->deleteLater();

    if (m_photoRemovedCallback) {
        m_photoRemovedCallback(absoluteFilePath);
    }

    if (removingCurrent && !nextPath.isEmpty()) {
        activatePhoto(nextPath);
        setCurrentPhoto(nextPath);
    }

    alignToDefaultPhotoBounds(true);
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
    if (filePath.isEmpty() || rating < 0 || rating > 5) {
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    if (rating == 0) {
        m_photoRatings.remove(absoluteFilePath);
    } else {
        m_photoRatings.insert(absoluteFilePath, rating);
    }
    ApplicationStateStore::setPhotoRating(absoluteFilePath, rating);

    QWidget *thumbnail = m_thumbnails.value(filePath, nullptr);
    if (!thumbnail) {
        thumbnail = m_thumbnails.value(absoluteFilePath, nullptr);
    }
    if (thumbnail) {
        static_cast<FilmstripThumbnail *>(thumbnail)->setRating(rating);
    }
}

void FilmstripWidget::setPhotoStatus(const QString &filePath, int status)
{
    if (filePath.isEmpty() || (status != 1 && status != -1)) {
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    m_photoStatuses.insert(absoluteFilePath, status);
    ApplicationStateStore::setPhotoStatus(absoluteFilePath, status);

    QWidget *thumbnail = m_thumbnails.value(filePath, nullptr);
    if (!thumbnail) {
        thumbnail = m_thumbnails.value(absoluteFilePath, nullptr);
    }
    if (thumbnail) {
        static_cast<FilmstripThumbnail *>(thumbnail)->setStatus(status);
    }
}

void FilmstripWidget::setPhotoColorLabel(const QString &filePath, int colorLabel)
{
    if (filePath.isEmpty() || colorLabel < 0 || colorLabel > 5) {
        return;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    if (colorLabel == 0) {
        m_photoColorLabels.remove(absoluteFilePath);
    } else {
        m_photoColorLabels.insert(absoluteFilePath, colorLabel);
    }
    ApplicationStateStore::setPhotoColorLabel(absoluteFilePath, colorLabel);

    QWidget *thumbnail = m_thumbnails.value(filePath, nullptr);
    if (!thumbnail) {
        thumbnail = m_thumbnails.value(absoluteFilePath, nullptr);
    }
    if (thumbnail) {
        static_cast<FilmstripThumbnail *>(thumbnail)->setColorLabel(colorLabel);
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
