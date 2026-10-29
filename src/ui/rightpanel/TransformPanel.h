#pragma once

#include "app/DevelopState.h"

#include <QString>
#include <QWidget>

class QButtonGroup;
class QCheckBox;
class QFrame;
class QSlider;
class QToolButton;
class QVBoxLayout;

class TransformPanel final : public QWidget
{
    Q_OBJECT

public:
    enum class ValueFormat {
        Integer,
        Degrees,
        Scale,
    };

    explicit TransformPanel(QWidget *parent = nullptr);

    void setFromState(const DevelopState &state);
    void applyToState(DevelopState &state) const;
    [[nodiscard]] bool isEditing() const;

signals:
    void previewRequested();
    void commitRequested(const QString &historyLabel);
    void uprightModeRequested(int mode);

private:
    QSlider *addSlider(QFrame *content,
                       QVBoxLayout *layout,
                       const QString &label,
                       int minimum,
                       int maximum,
                       int neutral,
                       ValueFormat format,
                       const QString &historyLabel);
    void resetAll();

    QFrame *m_content = nullptr;
    QToolButton *m_chevronButton = nullptr;
    QButtonGroup *m_uprightButtons = nullptr;
    QSlider *m_verticalSlider = nullptr;
    QSlider *m_horizontalSlider = nullptr;
    QSlider *m_rotateSlider = nullptr;
    QSlider *m_aspectSlider = nullptr;
    QSlider *m_scaleSlider = nullptr;
    QSlider *m_xOffsetSlider = nullptr;
    QSlider *m_yOffsetSlider = nullptr;
    QCheckBox *m_constrainCrop = nullptr;
    int m_uprightMode = 0;
    bool m_settingControls = false;
};
