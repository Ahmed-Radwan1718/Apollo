#pragma once

#include "app/DevelopState.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

#include <optional>

class HistoryModel final
{
public:
    struct Entry {
        QString label;
        DevelopState state;
    };

    void initializePhoto(const QString &filePath, const DevelopState &originalState);
    [[nodiscard]] bool hasPhoto(const QString &filePath) const;
    [[nodiscard]] QStringList labels(const QString &filePath) const;
    [[nodiscard]] int currentIndex(const QString &filePath) const;
    [[nodiscard]] std::optional<DevelopState> currentState(const QString &filePath) const;
    [[nodiscard]] std::optional<DevelopState> selectEntry(const QString &filePath, int index);
    bool commit(const QString &filePath, const QString &label, const DevelopState &state);

private:
    struct PhotoHistory {
        QVector<Entry> entries;
        int currentIndex = -1;
    };

    QHash<QString, PhotoHistory> m_histories;
};
