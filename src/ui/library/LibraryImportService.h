#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <atomic>
#include <cstdint>
#include <functional>

class LibraryImportService final : public QObject
{
public:
    using PhotoReadyCallback = std::function<void(const QString &, const QImage &)>;
    using DevelopPhotoReadyCallback = std::function<void(const QString &, const QString &)>;

    explicit LibraryImportService(QObject *parent = nullptr);
    ~LibraryImportService() override;

    void importFiles(const QStringList &filePaths);
    void requestDevelopPhoto(const QString &filePath);
    void setPhotoReadyCallback(PhotoReadyCallback callback);
    void setDevelopPhotoReadyCallback(DevelopPhotoReadyCallback callback);

private:
    QThreadPool m_pool;
    QThreadPool m_developPool;
    QSet<QString> m_pendingFiles;
    PhotoReadyCallback m_photoReady;
    DevelopPhotoReadyCallback m_developPhotoReady;
    std::atomic<std::uint64_t> m_developRequestSerial{0};
};
