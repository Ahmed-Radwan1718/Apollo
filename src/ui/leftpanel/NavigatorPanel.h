#pragma once

#include <QImage>
#include <QWidget>

class NavigatorPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit NavigatorPanel(QWidget *parent = nullptr);

    void setImage(const QImage &image);

private:
    class PreviewWidget;
    PreviewWidget *m_preview = nullptr;
};
