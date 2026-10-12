#include "imaging/TemperatureRenderService.h"

#include "imaging/ArwDecoder.h"

#include <QMetaObject>

#include <utility>

namespace imaging {

TemperatureRenderService::TemperatureRenderService(QObject *parent)
    : QObject(parent)
    , m_previewThread([this]() { workerLoop(true); })
    , m_fullThread([this]() { workerLoop(false); })
{
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
}

void TemperatureRenderService::requestPreview(const QString &filePath,
                                              int kelvin,
                                              int tint,
                                              int exposureHundredths,
                                              int highlights,
                                              int shadows,
                                              int whites,
                                              int contrast,
                                              int texture,
                                              int saturation,
                                              int vibrance)
{
    request({filePath, kelvin, tint, exposureHundredths, highlights, shadows,
             whites, contrast, texture, saturation, vibrance},
            true);
}

void TemperatureRenderService::requestFullRender(const QString &filePath,
                                                 int kelvin,
                                                 int tint,
                                                 int exposureHundredths,
                                                 int highlights,
                                                 int shadows,
                                                 int whites,
                                                 int contrast,
                                                 int texture,
                                                 int saturation,
                                                 int vibrance)
{
    request({filePath, kelvin, tint, exposureHundredths, highlights, shadows,
             whites, contrast, texture, saturation, vibrance},
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
        // Keep only the newest develop state. A RAW render already in progress may
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

        QString error;
        QImage image = ArwDecoder::decode(request.filePath,
                                          &error,
                                          nullptr,
                                          request.kelvin,
                                          request.tint,
                                          request.exposureHundredths,
                                          request.highlights,
                                          request.shadows,
                                          request.whites,
                                          request.contrast,
                                          request.texture,
                                          request.saturation,
                                          request.vibrance,
                                          preview);

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
                                 request.contrast,
                                 request.texture,
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
