#pragma once

#include <QImage>
#include <QSet>
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
    QScrollArea *m_scrollArea = nullptr;
    QWidget *m_content = nullptr;
    QHBoxLayout *m_contentLayout = nullptr;
    QSet<QString> m_importedPaths;
};
