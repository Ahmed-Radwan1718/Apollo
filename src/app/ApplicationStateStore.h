#pragma once

#include "app/HistoryModel.h"

#include <QString>
#include <QStringList>
#include <QVector>

class ApplicationStateStore final
{
public:
    ApplicationStateStore() = delete;

    [[nodiscard]] static QStringList loadLibraryPhotos();
    static void addLibraryPhoto(const QString &filePath);

    [[nodiscard]] static QVector<HistoryModel::Snapshot> loadHistories();
    static void saveHistories(const QVector<HistoryModel::Snapshot> &histories);
};
