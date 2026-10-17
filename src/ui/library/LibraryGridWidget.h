#pragma once

#include <QHash>
#include <QImage>
#include <QString>
#include <QVector>
#include <QWidget>

#include <functional>

class QGridLayout;
class QLabel;
class QResizeEvent;
class QScrollArea;
class QStackedWidget;

class LibraryGridWidget final : public QWidget
{
public:
    explicit LibraryGridWidget(QWidget *parent = nullptr);

    void addPhoto(const QString &filePath, const QImage &image);
    void setPhotoActivatedCallback(std::function<void(const QString &)> callback);

protected:
    void resizeEvent(QResizeEvent *event) override;

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
    std::function<void(const QString &)> m_photoActivated;
};
