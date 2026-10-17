#include "ui/canvas/GpuCanvasInstaller.h"

#include "ui/canvas/GpuCanvasViewport.h"
#include "ui/canvas/PhotoCanvas.h"

#include <QGraphicsView>
#include <QOpenGLContext>
#include <QSurfaceFormat>
#include <QWidget>

bool GpuCanvasInstaller::install(QWidget *root)
{
    if (!root) {
        return false;
    }

    PhotoCanvas *canvas = root->findChild<PhotoCanvas *>();
    if (!canvas || !canvas->viewport()) {
        return false;
    }

    QOpenGLContext probe;
    QSurfaceFormat format = QSurfaceFormat::defaultFormat();
    format.setRenderableType(QSurfaceFormat::OpenGL);
    probe.setFormat(format);
    if (!probe.create()) {
        return false;
    }

    QWidget *oldViewport = canvas->viewport();
    QWidget *overlay = oldViewport->findChild<QWidget *>(
        QStringLiteral("beforeAfterToggle"), Qt::FindDirectChildrenOnly);
    if (overlay) {
        overlay->setParent(nullptr);
    }

    auto *gpuViewport = new GpuCanvasViewport(canvas);
    gpuViewport->setAcceptDrops(true);
    gpuViewport->setMouseTracking(true);

    canvas->setViewport(gpuViewport);
    canvas->setViewportUpdateMode(QGraphicsView::FullViewportUpdate);

    if (overlay) {
        overlay->setParent(gpuViewport);
        overlay->show();
        overlay->raise();
    }

    return true;
}
