#pragma once

#include <QOpenGLWidget>

class GpuCanvasViewport final : public QOpenGLWidget
{
public:
    explicit GpuCanvasViewport(QWidget *parent = nullptr);
};
