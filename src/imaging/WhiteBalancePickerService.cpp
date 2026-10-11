#include "imaging/WhiteBalancePickerService.h"

#include "imaging/ArwDecoder.h"

#include <QMetaObject>

#include <utility>

namespace imaging {

WhiteBalancePickerService::WhiteBalancePickerService(QObject *parent)
    : QObject(parent)
    , m_thread([this]() { workerLoop(); })
{
}

WhiteBalancePickerService::~WhiteBalancePickerService()
{
    m_stopping.store(true, std::memory_order_release);
    m_condition.notify_all();

    if (m_thread.joinable()) {
        m_thread.join();
    }
}

void WhiteBalancePickerService::request(const QString &filePath,
                                        const QPointF &normalizedPosition)
{
    if (m_stopping.load(std::memory_order_acquire)) {
        return;
    }

    const std::uint64_t sequence =
        m_latestSequence.fetch_add(1, std::memory_order_acq_rel) + 1;

    {
        std::lock_guard lock(m_mutex);
        m_pending = Request{filePath, normalizedPosition, sequence};
    }

    m_condition.notify_one();
}

void WhiteBalancePickerService::workerLoop()
{
    while (!m_stopping.load(std::memory_order_acquire)) {
        Request request;

        {
            std::unique_lock lock(m_mutex);
            m_condition.wait(lock, [this]() {
                return m_stopping.load(std::memory_order_acquire) || m_pending.has_value();
            });

            if (m_stopping.load(std::memory_order_acquire)) {
                return;
            }

            request = std::move(*m_pending);
            m_pending.reset();
        }

        const WhiteBalancePickResult result =
            ArwDecoder::pickWhiteBalance(request.filePath, request.normalizedPosition);

        QMetaObject::invokeMethod(
            this,
            [this, request = std::move(request), result]() mutable {
                if (request.sequence != m_latestSequence.load(std::memory_order_acquire)) {
                    return;
                }

                emit resultReady(request.filePath,
                                 result.temperatureKelvin,
                                 result.tint,
                                 result.error);
            },
            Qt::QueuedConnection);
    }
}

} // namespace imaging
