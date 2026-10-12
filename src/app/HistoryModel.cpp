#include "app/HistoryModel.h"

#include <utility>

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
        && history.entries[history.currentIndex].state == state) {
        return false;
    }

    const int keepCount = history.currentIndex + 1;
    if (keepCount >= 0 && keepCount < history.entries.size()) {
        history.entries.resize(keepCount);
    }

    history.entries.push_back({label, state});
    history.currentIndex = history.entries.size() - 1;
    return true;
}
