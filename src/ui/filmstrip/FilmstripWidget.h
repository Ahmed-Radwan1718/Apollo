#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QWidget>

#include <functional>

class PhotoCanvas;
class QEvent;
class QHBoxLayout;
class QObject;
class QScrollArea;

class FilmstripWidget final : public QWidget
{
public:
    using PhotoOpenedCallback = std::function<void(const QString &, const QImage &)>;
    using PhotoRemovedCallback = std::function<void(const QString &)>;

    explicit FilmstripWidget(QWidget *parent = nullptr);

    void addPhoto(const QString &filePath, const QImage &image);
    void setPhotoOpenedCallback(PhotoOpenedCallback callback);
    void setPhotoRemovedCallback(PhotoRemovedCallback callback);
    [[nodiscard]] const QString &currentPhotoPath() const noexcept
    {
        return m_currentFilePath;
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void activatePhoto(const QString &filePath);
    void removePhoto(const QString &filePath);
    void setCurrentPhoto(const QString &filePath);
    void setPhotoRating(const QString &filePath, int rating);
    void setPhotoStatus(const QString &filePath, int status);
    void setPhotoColorLabel(const QString &filePath, int colorLabel);
    void alignToDefaultPhotoBounds(bool updateWhileManual = false);

    PhotoCanvas *m_canvas = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
    QHash<QString, QWidget *> m_thumbnails;
    QHash<QString, int> m_photoRatings;
    QHash<QString, int> m_photoStatuses;
    QHash<QString, int> m_photoColorLabels;
    QString m_currentFilePath;
    PhotoOpenedCallback m_photoOpenedCallback;
    PhotoRemovedCallback m_photoRemovedCallback;
};
