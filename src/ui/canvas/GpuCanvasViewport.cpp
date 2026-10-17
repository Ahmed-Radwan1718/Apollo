#include "ui/canvas/GpuCanvasViewport.h"

#include <QSurfaceFormat>

GpuCanvasViewport::GpuCanvasViewport(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setObjectName(QStringLiteral("gpuCanvasViewport"));
    setUpdateBehavior(QOpenGLWidget::NoPartialUpdate);
    setAutoFillBackground(false);

    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    format.setDepthBufferSize(0);
    format.setStencilBufferSize(0);
    setFormat(format);
}
