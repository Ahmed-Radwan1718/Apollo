#pragma once

#include <QObject>

class QAction;
class QWidget;
class FilmstripWidget;

class PhotoExportController final : public QObject
{
    Q_OBJECT

public:
    explicit PhotoExportController(QWidget *window, QObject *parent = nullptr);

private:
    void exportCurrentPhoto();

    QWidget *m_window = nullptr;
    FilmstripWidget *m_filmstrip = nullptr;
    QAction *m_exportAction = nullptr;
};
