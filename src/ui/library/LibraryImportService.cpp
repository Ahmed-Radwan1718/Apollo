#include "ui/library/LibraryImportService.h"

#include "imaging/ImageLoader.h"
#include "imaging/ThumbnailLoader.h"

#include <QFileInfo>
#include <QMetaObject>

#include <utility>

namespace {

constexpr int kLibraryThumbnailMaximumEdge = 640;
constexpr int kImportWorkerCount = 2;
constexpr int kDevelopWorkerCount = 2;

} // namespace

LibraryImportService::LibraryImportService(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(kImportWorkerCount);
    m_pool.setExpiryTimeout(10000);

    m_developPool.setMaxThreadCount(kDevelopWorkerCount);
    m_developPool.setExpiryTimeout(10000);
}

LibraryImportService::~LibraryImportService()
{
    m_developRequestSerial.fetch_add(1, std::memory_order_acq_rel);

    m_pool.clear();
    m_developPool.clear();
    m_pool.waitForDone();
    m_developPool.waitForDone();
}

void LibraryImportService::importFiles(const QStringList &filePaths)
{
    for (const QString &filePath : filePaths) {
        const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
        if (absoluteFilePath.isEmpty() || m_pendingFiles.contains(absoluteFilePath)) {
            continue;
        }

        m_pendingFiles.insert(absoluteFilePath);

        m_pool.start([this, absoluteFilePath]() {
            QImage thumbnail = imaging::ThumbnailLoader::load(
                absoluteFilePath,
                kLibraryThumbnailMaximumEdge);

            QMetaObject::invokeMethod(
                this,
                [this,
                 absoluteFilePath,
                 thumbnail = std::move(thumbnail)]() mutable {
                    m_pendingFiles.remove(absoluteFilePath);

                    if (!thumbnail.isNull() && m_photoReady) {
                        m_photoReady(absoluteFilePath, thumbnail);
                    }
                },
                Qt::QueuedConnection);
        });
    }
}

void LibraryImportService::requestDevelopPhoto(const QString &filePath)
{
    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    if (absoluteFilePath.isEmpty()) {
        return;
    }

    const std::uint64_t requestSerial =
        m_developRequestSerial.fetch_add(1, std::memory_order_acq_rel) + 1;

    // Remove work that has not started yet. A decoder already running cannot be
    // interrupted safely, but its result is ignored by the serial check below.
    m_developPool.clear();

    m_developPool.start([this, absoluteFilePath, requestSerial]() {
        const imaging::ImageLoadResult result = imaging::ImageLoader::load(absoluteFilePath);
        QString error = result.succeeded()
            ? QString()
            : result.error;
        if (!result.succeeded() && error.isEmpty()) {
            error = QStringLiteral("The photo could not be decoded.");
        }

        QMetaObject::invokeMethod(
            this,
            [this,
             absoluteFilePath,
             requestSerial,
             error = std::move(error)]() mutable {
                if (requestSerial
                    != m_developRequestSerial.load(std::memory_order_acquire)) {
                    return;
                }

                if (m_developPhotoReady) {
                    m_developPhotoReady(absoluteFilePath, error);
                }
            },
            Qt::QueuedConnection);
    });
}

void LibraryImportService::setPhotoReadyCallback(PhotoReadyCallback callback)
{
    m_photoReady = std::move(callback);
}

void LibraryImportService::setDevelopPhotoReadyCallback(
    DevelopPhotoReadyCallback callback)
{
    m_developPhotoReady = std::move(callback);
}
