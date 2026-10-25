#pragma once

#include "app/HistoryModel.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

class ApplicationStateStore final
{
public:
    ApplicationStateStore() = delete;

    [[nodiscard]] static QStringList loadLibraryPhotos();
    static void addLibraryPhoto(const QString &filePath);

    [[nodiscard]] static QHash<QString, int> loadPhotoRatings();
    static void setPhotoRating(const QString &filePath, int rating);

    [[nodiscard]] static QHash<QString, int> loadPhotoStatuses();
    static void setPhotoStatus(const QString &filePath, int status);

    [[nodiscard]] static QHash<QString, int> loadPhotoColorLabels();
    static void setPhotoColorLabel(const QString &filePath, int colorLabel);

    [[nodiscard]] static QVector<HistoryModel::Snapshot> loadHistories();
    static void saveHistories(const QVector<HistoryModel::Snapshot> &histories);
};
