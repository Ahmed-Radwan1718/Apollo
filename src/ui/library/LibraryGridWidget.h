#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include <functional>

class QDragEnterEvent;
class QDropEvent;
class QEvent;
class QGridLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;

class LibraryGridWidget final : public QWidget
{
public:
    using PhotoActivatedCallback =
        std::function<void(const QString &, const QImage &)>;
    using FilesDroppedCallback = std::function<void(const QStringList &)>;

    explicit LibraryGridWidget(QWidget *parent = nullptr);

    void addPhoto(const QString &filePath, const QImage &image);
    void setPhotoActivatedCallback(PhotoActivatedCallback callback);
    void setFilesDroppedCallback(FilesDroppedCallback callback);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void reflow();
    void setCurrentPhoto(const QString &filePath);
    void updateHeader();

    QLabel *m_countLabel = nullptr;
    QStackedWidget *m_stack = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QGridLayout *m_gridLayout = nullptr;
    QHash<QString, QWidget *> m_thumbnails;
    QVector<QString> m_order;
    QString m_currentFilePath;
    PhotoActivatedCallback m_photoActivated;
    FilesDroppedCallback m_filesDropped;
};
