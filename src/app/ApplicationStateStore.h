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

    [[nodiscard]] static QVector<HistoryModel::Snapshot> loadHistories();
    static void saveHistories(const QVector<HistoryModel::Snapshot> &histories);
};
