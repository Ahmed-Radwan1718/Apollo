#pragma once

#include <QImage>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>

#include <functional>

class LibraryImportService final : public QObject
{
public:
    using PhotoReadyCallback = std::function<void(const QString &, const QImage &)>;

    explicit LibraryImportService(QObject *parent = nullptr);
    ~LibraryImportService() override;

    void importFiles(const QStringList &filePaths);
    void setPhotoReadyCallback(PhotoReadyCallback callback);

private:
    QThreadPool m_pool;
    QSet<QString> m_pendingFiles;
    PhotoReadyCallback m_photoReady;
};
