#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QWidget>

class PhotoCanvas;
class QEvent;
class QHBoxLayout;
class QObject;
class QScrollArea;

class FilmstripWidget final : public QWidget
{
public:
    explicit FilmstripWidget(QWidget *parent = nullptr);

    void addPhoto(const QString &filePath, const QImage &image);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void activatePhoto(const QString &filePath);
    void setCurrentPhoto(const QString &filePath);
    void alignToDefaultPhotoBounds(bool updateWhileManual = false);

    PhotoCanvas *m_canvas = nullptr;
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
    QHash<QString, QWidget *> m_thumbnails;
    QString m_currentFilePath;
};
