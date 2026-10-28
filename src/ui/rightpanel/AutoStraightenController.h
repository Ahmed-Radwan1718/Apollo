#pragma once

#include <QObject>

class PhotoCanvas;
class QLabel;
class QEvent;
class QGraphicsItem;
class QImage;
class QSlider;
class QWidget;

class AutoStraightenController final : public QObject
{
public:
    explicit AutoStraightenController(QWidget *root, QObject *parent = nullptr);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void applyAutoStraighten();
    [[nodiscard]] QGraphicsItem *imageItem() const;
    [[nodiscard]] QImage analysisImage(QGraphicsItem *item) const;

    PhotoCanvas *m_canvas = nullptr;
    QLabel *m_straightenLabel = nullptr;
    QSlider *m_straightenSlider = nullptr;
};
