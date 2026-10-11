#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QWidget>

class QHBoxLayout;
class QScrollArea;

class FilmstripWidget final : public QWidget
{
public:
    explicit FilmstripWidget(QWidget *parent = nullptr);

    void addPhoto(const QString &filePath, const QImage &image);

private:
    void activatePhoto(const QString &filePath);
    void setCurrentPhoto(const QString &filePath);

    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
    QHash<QString, QWidget *> m_thumbnails;
    QString m_currentFilePath;
};
