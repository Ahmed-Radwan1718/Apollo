#pragma once

#include <QImage>
#include <QWidget>

class PhotoCanvas;
class QComboBox;
class QShowEvent;

class NavigatorPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit NavigatorPanel(QWidget *parent = nullptr);

    void setImage(const QImage &image);

protected:
    void showEvent(QShowEvent *event) override;

private:
    class PreviewWidget;

    void bindCanvas();
    void setZoomState(qreal percent, bool fitMode);

    PreviewWidget *m_preview = nullptr;
    QComboBox *m_zoomCombo = nullptr;
    PhotoCanvas *m_canvas = nullptr;
};
