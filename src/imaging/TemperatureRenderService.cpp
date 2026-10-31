#include "imaging/TemperatureRenderService.h"

#include "imaging/ArwDecoder.h"
#include "imaging/ArwPreviewSession.h"
#include "imaging/DetailProcessor.h"

#include <QMetaObject>

#include <memory>
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

void TemperatureRenderService::setDetailSettings(const QString &filePath,
                                                  const DetailSettings &settings)
{
    if (filePath.isEmpty()) {
        return;
    }

    std::lock_guard lock(m_detailMutex);
    m_detailSettings.insert(filePath, settings);
}

DetailSettings TemperatureRenderService::detailSettingsForFile(
    const QString &filePath) const
{
    std::lock_guard lock(m_detailMutex);
    return m_detailSettings.value(filePath, DetailSettings{});
}

bool TemperatureRenderService::detailRequestIsCurrent(const Request &request) const
{
    return request.detail == detailSettingsForFile(request.filePath);
}

void TemperatureRenderService::preparePreview(const QString &filePath,
                                              int kelvin,
                                              int tint,
                                              const RawCalibration &calibration)
{
    Request request;
    request.filePath = filePath;
    request.kelvin = kelvin;
    request.tint = tint;
    request.calibration = calibration;
    request.prepareOnly = true;
    this->request(std::move(request), true);
}

void TemperatureRenderService::requestPreview(const QString &filePath,
                                              int kelvin,
                                              int tint,
                                              const RawCalibration &calibration,
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
                                              int vibrance,
                                              const ColorMixerSettings &colorMixer)
{
    Request renderRequest;
    renderRequest.filePath = filePath;
    renderRequest.kelvin = kelvin;
    renderRequest.tint = tint;
    renderRequest.calibration = calibration;
    renderRequest.exposureHundredths = exposureHundredths;
    renderRequest.highlights = highlights;
    renderRequest.shadows = shadows;
    renderRequest.whites = whites;
    renderRequest.blacks = blacks;
    renderRequest.contrast = contrast;
    renderRequest.texture = texture;
    renderRequest.clarity = clarity;
    renderRequest.dehaze = dehaze;
    renderRequest.saturation = saturation;
    renderRequest.vibrance = vibrance;
    renderRequest.colorMixer = colorMixer;
    request(std::move(renderRequest), true);
}

void TemperatureRenderService::requestFullRender(const QString &filePath,
                                                 int kelvin,
                                                 int tint,
                                                 const RawCalibration &calibration,
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
                                                 int vibrance,
                                                 const ColorMixerSettings &colorMixer)
{
    {
        std::lock_guard lock(m_previewMutex);
        m_pendingPreview.reset();
    }

    Request renderRequest;
    renderRequest.filePath = filePath;
    renderRequest.kelvin = kelvin;
    renderRequest.tint = tint;
    renderRequest.calibration = calibration;
    renderRequest.exposureHundredths = exposureHundredths;
    renderRequest.highlights = highlights;
    renderRequest.shadows = shadows;
    renderRequest.whites = whites;
    renderRequest.blacks = blacks;
    renderRequest.contrast = contrast;
    renderRequest.texture = texture;
    renderRequest.clarity = clarity;
    renderRequest.dehaze = dehaze;
    renderRequest.saturation = saturation;
    renderRequest.vibrance = vibrance;
    renderRequest.colorMixer = colorMixer;
    request(std::move(renderRequest), false);
}

void TemperatureRenderService::request(Request request, bool preview)
{
    if (m_stopping.load(std::memory_order_acquire)) {
        return;
    }

    request.detail = detailSettingsForFile(request.filePath);

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
        previewSession = std::make_unique<ArwPreviewSession>();
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
                                    request.calibration,
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
                                           request.calibration,
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
                                           request.colorMixer);
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
                                       request.calibration,
                                       false,
                                       request.colorMixer);
        }

        if (!image.isNull()) {
            image = DetailProcessor::apply(image,
                                           request.detail,
                                           preview ? 0.5 : 1.0);
        }

        // Detail is intentionally carried outside the legacy renderReady signal.
        // If a newer Detail state arrived while this render was running, discard
        // this result rather than letting an obsolete preview/full render flash.
        if (!detailRequestIsCurrent(request)) {
            continue;
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
                                 request.calibration,
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
                                 request.colorMixer,
                                 preview,
                                 image,
                                 error);
            },
            Qt::QueuedConnection);
    }
}

} // namespace imaging
