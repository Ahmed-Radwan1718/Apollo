#include "app/HistoryModel.h"

#include "app/ApplicationStateStore.h"

#include <algorithm>
#include <utility>

HistoryModel::HistoryModel()
{
    const QVector<Snapshot> savedHistories = ApplicationStateStore::loadHistories();
    for (const Snapshot &snapshot : savedHistories) {
        if (snapshot.filePath.isEmpty() || snapshot.entries.isEmpty()) {
            continue;
        }

        PhotoHistory history;
        history.entries = snapshot.entries;
        history.currentIndex = std::clamp(snapshot.currentIndex,
                                          0,
                                          static_cast<int>(history.entries.size()) - 1);
        m_histories.insert(snapshot.filePath, std::move(history));
    }
}

void HistoryModel::initializePhoto(const QString &filePath,
                                   const DevelopState &originalState)
{
    if (filePath.isEmpty() || m_histories.contains(filePath)) {
        return;
    }

    PhotoHistory history;
    history.entries.push_back({QStringLiteral("Original"), originalState});
    history.currentIndex = 0;
    m_histories.insert(filePath, std::move(history));
    persist();
}

bool HistoryModel::hasPhoto(const QString &filePath) const
{
    return m_histories.contains(filePath);
}

QStringList HistoryModel::labels(const QString &filePath) const
{
    QStringList result;
    const auto iterator = m_histories.constFind(filePath);
    if (iterator == m_histories.constEnd()) {
        return result;
    }

    result.reserve(iterator->entries.size());
    for (const Entry &entry : iterator->entries) {
        result.push_back(entry.label);
    }

    return result;
}

int HistoryModel::currentIndex(const QString &filePath) const
{
    const auto iterator = m_histories.constFind(filePath);
    return iterator == m_histories.constEnd() ? -1 : iterator->currentIndex;
}

std::optional<DevelopState> HistoryModel::currentState(const QString &filePath) const
{
    const auto iterator = m_histories.constFind(filePath);
    if (iterator == m_histories.constEnd()
        || iterator->currentIndex < 0
        || iterator->currentIndex >= iterator->entries.size()) {
        return std::nullopt;
    }

    return iterator->entries[iterator->currentIndex].state;
}

std::optional<DevelopState> HistoryModel::selectEntry(const QString &filePath, int index)
{
    auto iterator = m_histories.find(filePath);
    if (iterator == m_histories.end() || index < 0 || index >= iterator->entries.size()) {
        return std::nullopt;
    }

    iterator->currentIndex = index;
    persist();
    return iterator->entries[index].state;
}

bool HistoryModel::commit(const QString &filePath,
                          const QString &label,
                          const DevelopState &state)
{
    auto iterator = m_histories.find(filePath);
    if (iterator == m_histories.end() || label.isEmpty()) {
        return false;
    }

    PhotoHistory &history = iterator.value();
    if (history.currentIndex >= 0
        && history.currentIndex < history.entries.size()
        && history.entries[history.currentIndex].state.fullyEquals(state)) {
        return false;
    }

    const int keepCount = history.currentIndex + 1;
    if (keepCount >= 0 && keepCount < history.entries.size()) {
        history.entries.resize(keepCount);
    }

    history.entries.push_back({label, state});
    history.currentIndex = static_cast<int>(history.entries.size()) - 1;
    persist();
    return true;
}

QVector<HistoryModel::Snapshot> HistoryModel::snapshots() const
{
    QVector<Snapshot> result;
    result.reserve(m_histories.size());

    for (auto iterator = m_histories.constBegin(); iterator != m_histories.constEnd(); ++iterator) {
        result.push_back({iterator.key(), iterator->entries, iterator->currentIndex});
    }

    std::sort(result.begin(), result.end(),
              [](const Snapshot &left, const Snapshot &right) {
                  return left.filePath < right.filePath;
              });
    return result;
}

void HistoryModel::persist() const
{
    ApplicationStateStore::saveHistories(snapshots());
}
