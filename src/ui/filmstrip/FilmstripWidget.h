#pragma once

#include <QHash>
#include <QImage>
#include <QSet>
#include <QString>
#include <QStringList>
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
    void handleThumbnailActivated(const QString &filePath, bool additiveSelection);
    void prepareContextSelection(const QString &filePath);
    void removePhoto(const QString &filePath);
    void setCurrentPhoto(const QString &filePath);
    void setPhotoRating(const QString &filePath, int rating);
    void setPhotoStatus(const QString &filePath, int status);
    void setPhotoColorLabel(const QString &filePath, int colorLabel);
    void updateSelectionVisuals();
    [[nodiscard]] QStringList selectedPhotoPaths() const;
    [[nodiscard]] QString firstSelectedPhotoPath() const;
    [[nodiscard]] bool canSyncSettings() const;
    void syncSelectedSettings();
    void alignToDefaultPhotoBounds(bool updateWhileManual = false);

    PhotoCanvas *m_canvas = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
    QHash<QString, QWidget *> m_thumbnails;
    QHash<QString, int> m_photoRatings;
    QHash<QString, int> m_photoStatuses;
    QHash<QString, int> m_photoColorLabels;
    QSet<QString> m_selectedFilePaths;
    QString m_currentFilePath;
    PhotoOpenedCallback m_photoOpenedCallback;
    PhotoRemovedCallback m_photoRemovedCallback;
};
