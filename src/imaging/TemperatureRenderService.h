#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

class QOffscreenSurface;

namespace imaging {

class TemperatureRenderService final : public QObject
{
    Q_OBJECT

public:
    explicit TemperatureRenderService(QObject *parent = nullptr);
    ~TemperatureRenderService() override;

    void preparePreview(const QString &filePath,
                        int kelvin,
                        int tint);
    void requestPreview(const QString &filePath,
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
                        int vibrance);
    void requestFullRender(const QString &filePath,
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
                           int vibrance);

signals:
    void renderReady(const QString &filePath,
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
                     int vibrance,
                     bool preview,
                     const QImage &image,
                     const QString &error);

private:
    struct Request {
        QString filePath;
        int kelvin = 0;
        int tint = 0;
        int exposureHundredths = 0;
        int highlights = 0;
        int shadows = 0;
        int whites = 0;
        int blacks = 0;
        int contrast = 0;
        int texture = 0;
        int clarity = 0;
        int dehaze = 0;
        int saturation = 0;
        int vibrance = 0;
        bool prepareOnly = false;
    };

    void request(Request request, bool preview);
    void workerLoop(bool preview);

    std::atomic_bool m_stopping = false;

    std::mutex m_previewMutex;
    std::condition_variable m_previewCondition;
    std::optional<Request> m_pendingPreview;

    std::mutex m_fullMutex;
    std::condition_variable m_fullCondition;
    std::optional<Request> m_pendingFull;

    QOffscreenSurface *m_previewSurface = nullptr;
    std::jthread m_previewThread;
    std::jthread m_fullThread;
};

} // namespace imaging
