#include "imaging/TemperatureRenderService.h"

#include "imaging/ArwDecoder.h"
#include "imaging/ArwPreviewSession.h"

#include <QMetaObject>
#include <QOffscreenSurface>
#include <QOpenGLContext>
#include <QSurfaceFormat>

#include <memory>
#include <utility>

namespace imaging {

TemperatureRenderService::TemperatureRenderService(QObject *parent)
    : QObject(parent)
{
    // QOffscreenSurface must be created and destroyed on the GUI thread. The
    // preview worker may safely make its own OpenGL context current against this
    // surface after creation, which keeps all shader work off the UI thread.
    if (QOpenGLContext::supportsThreadedOpenGL()) {
        QSurfaceFormat format;
        format.setRenderableType(QSurfaceFormat::OpenGL);
        format.setVersion(3, 3);
        format.setProfile(QSurfaceFormat::CoreProfile);
        format.setDepthBufferSize(0);
        format.setStencilBufferSize(0);

        auto *surface = new QOffscreenSurface();
        surface->setFormat(format);
        surface->create();
        if (surface->isValid()) {
            m_previewSurface = surface;
        } else {
            delete surface;
        }
    }

    m_previewThread = std::jthread([this]() { workerLoop(true); });
    m_fullThread = std::jthread([this]() { workerLoop(false); });
}

TemperatureRenderService::~TemperatureRenderService()
{
    m_stopping.store(true, std::memory_order_release);
    m_previewCondition.notify_all();
    m_fullCondition.notify_all();

    if (m_previewThread.joinable()) {
        m_previewThread.join();
    }
    if (m_fullThread.joinable()) {
        m_fullThread.join();
    }

    // The worker context has been destroyed by this point, so the GUI thread can
    // safely release the offscreen surface it created in the constructor.
    delete m_previewSurface;
    m_previewSurface = nullptr;
}

void TemperatureRenderService::preparePreview(const QString &filePath,
                                              int kelvin,
                                              int tint)
{
    Request request;
    request.filePath = filePath;
    request.kelvin = kelvin;
    request.tint = tint;
    request.prepareOnly = true;
    this->request(std::move(request), true);
}

void TemperatureRenderService::requestPreview(const QString &filePath,
                                              int kelvin,
                                              int tint,
                                              int exposureHundredths,
                                              int highlights,
                                              int shadows,
                                              int whites,
                                              int blacks,
                                              int contrast,
                                              int texture,
                                              int clarity,
                                              int dehaze,
                                              int saturation,
                                              int vibrance)
{
    request({filePath, kelvin, tint, exposureHundredths, highlights, shadows,
             whites, blacks, contrast, texture, clarity, dehaze, saturation, vibrance},
            true);
}

void TemperatureRenderService::requestFullRender(const QString &filePath,
                                                 int kelvin,
                                                 int tint,
                                                 int exposureHundredths,
                                                 int highlights,
                                                 int shadows,
                                                 int whites,
                                                 int blacks,
                                                 int contrast,
                                                 int texture,
                                                 int clarity,
                                                 int dehaze,
                                                 int saturation,
                                                 int vibrance)
{
    {
        std::lock_guard lock(m_previewMutex);
        m_pendingPreview.reset();
    }

    request({filePath, kelvin, tint, exposureHundredths, highlights, shadows,
             whites, blacks, contrast, texture, clarity, dehaze, saturation, vibrance},
            false);
}

void TemperatureRenderService::request(Request request, bool preview)
{
    if (m_stopping.load(std::memory_order_acquire)) {
        return;
    }

    std::mutex &mutex = preview ? m_previewMutex : m_fullMutex;
    std::condition_variable &condition = preview ? m_previewCondition : m_fullCondition;
    std::optional<Request> &pending = preview ? m_pendingPreview : m_pendingFull;

    {
        std::lock_guard lock(mutex);
        // Keep only the newest develop state. A render already in progress may
        // finish, but obsolete develop-control positions never queue.
        pending = std::move(request);
    }

    condition.notify_one();
}

void TemperatureRenderService::workerLoop(bool preview)
{
    std::mutex &mutex = preview ? m_previewMutex : m_fullMutex;
    std::condition_variable &condition = preview ? m_previewCondition : m_fullCondition;
    std::optional<Request> &pending = preview ? m_pendingPreview : m_pendingFull;
    std::unique_ptr<ArwPreviewSession> previewSession;
    if (preview) {
        previewSession = std::make_unique<ArwPreviewSession>(m_previewSurface);
    }

    while (!m_stopping.load(std::memory_order_acquire)) {
        Request request;

        {
            std::unique_lock lock(mutex);
            condition.wait(lock, [this, &pending]() {
                return m_stopping.load(std::memory_order_acquire) || pending.has_value();
            });

            if (m_stopping.load(std::memory_order_acquire)) {
                return;
            }

            request = std::move(*pending);
            pending.reset();
        }

        if (preview && request.prepareOnly) {
            QString ignoredError;
            previewSession->prepare(request.filePath,
                                    request.kelvin,
                                    request.tint,
                                    &ignoredError);
            continue;
        }

        QString error;
        QImage image;
        if (preview) {
            image = previewSession->render(request.filePath,
                                           &error,
                                           request.kelvin,
                                           request.tint,
                                           request.exposureHundredths,
                                           request.highlights,
                                           request.shadows,
                                           request.whites,
                                           request.blacks,
                                           request.contrast,
                                           request.texture,
                                           request.clarity,
                                           request.dehaze,
                                           request.saturation,
                                           request.vibrance);
        } else {
            image = ArwDecoder::decode(request.filePath,
                                       &error,
                                       nullptr,
                                       request.kelvin,
                                       request.tint,
                                       request.exposureHundredths,
                                       request.highlights,
                                       request.shadows,
                                       request.whites,
                                       request.blacks,
                                       request.contrast,
                                       request.texture,
                                       request.clarity,
                                       request.dehaze,
                                       request.saturation,
                                       request.vibrance,
                                       false);
        }

        QMetaObject::invokeMethod(
            this,
            [this,
             request = std::move(request),
             image = std::move(image),
             error = std::move(error),
             preview]() mutable {
                emit renderReady(request.filePath,
                                 request.kelvin,
                                 request.tint,
                                 request.exposureHundredths,
                                 request.highlights,
                                 request.shadows,
                                 request.whites,
                                 request.blacks,
                                 request.contrast,
                                 request.texture,
                                 request.clarity,
                                 request.dehaze,
                                 request.saturation,
                                 request.vibrance,
                                 preview,
                                 image,
                                 error);
            },
            Qt::QueuedConnection);
    }
}

} // namespace imaging
