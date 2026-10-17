#include "ui/library/LibraryImportService.h"

#include "imaging/ThumbnailLoader.h"

#include <QFileInfo>
#include <QMetaObject>

#include <utility>

namespace {

constexpr int kLibraryThumbnailMaximumEdge = 640;
constexpr int kImportWorkerCount = 2;

} // namespace

LibraryImportService::LibraryImportService(QObject *parent)
    : QObject(parent)
{
    m_pool.setMaxThreadCount(kImportWorkerCount);
    m_pool.setExpiryTimeout(10000);
}

LibraryImportService::~LibraryImportService()
{
    m_pool.clear();
    m_pool.waitForDone();
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

void LibraryImportService::setPhotoReadyCallback(PhotoReadyCallback callback)
{
    m_photoReady = std::move(callback);
}
