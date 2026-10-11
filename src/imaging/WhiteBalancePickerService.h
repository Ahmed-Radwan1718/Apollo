#pragma once

#include <QObject>
#include <QPointF>
#include <QString>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>

namespace imaging {

class WhiteBalancePickerService final : public QObject
{
    Q_OBJECT

public:
    explicit WhiteBalancePickerService(QObject *parent = nullptr);
    ~WhiteBalancePickerService() override;

    void request(const QString &filePath, const QPointF &normalizedPosition);

signals:
    void resultReady(const QString &filePath,
                     int temperatureKelvin,
                     int tint,
                     const QString &error);

private:
    struct Request {
        QString filePath;
        QPointF normalizedPosition;
        std::uint64_t sequence = 0;
    };

    void workerLoop();

    std::atomic_bool m_stopping = false;
    std::atomic_uint64_t m_latestSequence = 0;

    std::mutex m_mutex;
    std::condition_variable m_condition;
    std::optional<Request> m_pending;
    std::jthread m_thread;
};

} // namespace imaging
