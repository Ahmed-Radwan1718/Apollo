#pragma once

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <thread>

namespace imaging {

class TemperatureRenderService final : public QObject
{
    Q_OBJECT

public:
    explicit TemperatureRenderService(QObject *parent = nullptr);
    ~TemperatureRenderService() override;

    void requestPreview(const QString &filePath,
                        int kelvin,
                        int tint,
                        int exposureHundredths);
    void requestFullRender(const QString &filePath,
                           int kelvin,
                           int tint,
                           int exposureHundredths);

signals:
    void renderReady(const QString &filePath,
                     int kelvin,
                     int tint,
                     int exposureHundredths,
                     bool preview,
                     const QImage &image,
                     const QString &error);

private:
    struct Request {
        QString filePath;
        int kelvin = 0;
        int tint = 0;
        int exposureHundredths = 0;
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

    std::jthread m_previewThread;
    std::jthread m_fullThread;
};

} // namespace imaging
