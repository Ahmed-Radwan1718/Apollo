#pragma once

#include <QObject>
#include <QPointF>

class BasicPanel;
class PhotoCanvas;
class QEvent;
class QLabel;
class QToolButton;

class WhiteBalancePickerController final : public QObject
{
    Q_OBJECT

public:
    WhiteBalancePickerController(BasicPanel *panel,
                                 PhotoCanvas *canvas,
                                 QObject *parent = nullptr);

    void setEnabled(bool enabled);
    void setActive(bool active);
    [[nodiscard]] bool isActive() const noexcept;

signals:
    void pointPicked(const QPointF &normalizedPosition);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void installButton(BasicPanel *panel);
    void positionButton();
    [[nodiscard]] bool sampledPatchLooksUsable(const QPointF &viewportPosition) const;
    void updateCursor();

    PhotoCanvas *m_canvas = nullptr;
    QLabel *m_hostLabel = nullptr;
    QToolButton *m_button = nullptr;
    bool m_active = false;
    Qt::CursorShape m_previousCursorShape = Qt::ArrowCursor;
};
