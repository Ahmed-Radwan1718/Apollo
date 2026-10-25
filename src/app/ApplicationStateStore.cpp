#include "app/ApplicationStateStore.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QStandardPaths>

#include <algorithm>
#include <utility>

namespace {

constexpr int kStateSchemaVersion = 4;
constexpr auto kStateFileName = "apollo-state.json";

QString stateFilePath()
{
    const QString directory =
        QStandardPaths::writableLocation(QStandardPaths::StateLocation);
    if (directory.isEmpty()) {
        return {};
    }

    return QDir(directory).filePath(QString::fromLatin1(kStateFileName));
}

QJsonObject emptyState()
{
    QJsonObject root;
    root.insert(QStringLiteral("version"), kStateSchemaVersion);
    return root;
}

QJsonObject loadStateObject()
{
    const QString filePath = stateFilePath();
    if (filePath.isEmpty()) {
        return emptyState();
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly)) {
        return emptyState();
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return emptyState();
    }

    return document.object();
}

void saveStateObject(QJsonObject root)
{
    const QString filePath = stateFilePath();
    if (filePath.isEmpty()) {
        return;
    }

    const QFileInfo fileInfo(filePath);
    if (!QDir().mkpath(fileInfo.absolutePath())) {
        return;
    }

    root.insert(QStringLiteral("version"), kStateSchemaVersion);
    const QByteArray payload = QJsonDocument(root).toJson(QJsonDocument::Compact);

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }

    if (file.write(payload) != payload.size()) {
        file.cancelWriting();
        return;
    }

    file.commit();
}

QJsonObject developStateToJson(const DevelopState &state)
{
    QJsonObject object;
    object.insert(QStringLiteral("temperatureKelvin"), state.temperatureKelvin);
    object.insert(QStringLiteral("tint"), state.tint);
    object.insert(QStringLiteral("exposureHundredths"), state.exposureHundredths);
    object.insert(QStringLiteral("highlights"), state.highlights);
    object.insert(QStringLiteral("shadows"), state.shadows);
    object.insert(QStringLiteral("whites"), state.whites);
    object.insert(QStringLiteral("blacks"), state.blacks);
    object.insert(QStringLiteral("contrast"), state.contrast);
    object.insert(QStringLiteral("texture"), state.texture);
    object.insert(QStringLiteral("clarity"), state.clarity);
    object.insert(QStringLiteral("dehaze"), state.dehaze);
    object.insert(QStringLiteral("saturation"), state.saturation);
    object.insert(QStringLiteral("vibrance"), state.vibrance);
    object.insert(QStringLiteral("cropX"), state.cropX);
    object.insert(QStringLiteral("cropY"), state.cropY);
    object.insert(QStringLiteral("cropWidth"), state.cropWidth);
    object.insert(QStringLiteral("cropHeight"), state.cropHeight);
    object.insert(QStringLiteral("cropQuarterTurns"), state.cropQuarterTurns);
    object.insert(QStringLiteral("cropStraightenHundredths"),
                  state.cropStraightenHundredths);
    object.insert(QStringLiteral("cropAspectMode"), state.cropAspectMode);
    return object;
}

DevelopState developStateFromJson(const QJsonObject &object)
{
    DevelopState state;
    state.temperatureKelvin = object.value(QStringLiteral("temperatureKelvin"))
                                  .toInt(state.temperatureKelvin);
    state.tint = object.value(QStringLiteral("tint")).toInt(state.tint);
    state.exposureHundredths = object.value(QStringLiteral("exposureHundredths"))
                                   .toInt(state.exposureHundredths);
    state.highlights = object.value(QStringLiteral("highlights")).toInt(state.highlights);
    state.shadows = object.value(QStringLiteral("shadows")).toInt(state.shadows);
    state.whites = object.value(QStringLiteral("whites")).toInt(state.whites);
    state.blacks = object.value(QStringLiteral("blacks")).toInt(state.blacks);
    state.contrast = object.value(QStringLiteral("contrast")).toInt(state.contrast);
    state.texture = object.value(QStringLiteral("texture")).toInt(state.texture);
    state.clarity = object.value(QStringLiteral("clarity")).toInt(state.clarity);
    state.dehaze = object.value(QStringLiteral("dehaze")).toInt(state.dehaze);
    state.saturation = object.value(QStringLiteral("saturation")).toInt(state.saturation);
    state.vibrance = object.value(QStringLiteral("vibrance")).toInt(state.vibrance);
    state.cropX = object.value(QStringLiteral("cropX")).toInt(state.cropX);
    state.cropY = object.value(QStringLiteral("cropY")).toInt(state.cropY);
    state.cropWidth = object.value(QStringLiteral("cropWidth")).toInt(state.cropWidth);
    state.cropHeight = object.value(QStringLiteral("cropHeight")).toInt(state.cropHeight);
    state.cropQuarterTurns = object.value(QStringLiteral("cropQuarterTurns"))
                                 .toInt(state.cropQuarterTurns);
    state.cropStraightenHundredths =
        object.value(QStringLiteral("cropStraightenHundredths"))
            .toInt(state.cropStraightenHundredths);
    state.cropAspectMode = object.value(QStringLiteral("cropAspectMode"))
                               .toInt(state.cropAspectMode);
    return state;
}

QString normalizedPath(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return {};
    }

    return QDir::cleanPath(QFileInfo(filePath).absoluteFilePath());
}

} // namespace

QStringList ApplicationStateStore::loadLibraryPhotos()
{
    QStringList result;
    const QJsonArray photos =
        loadStateObject().value(QStringLiteral("libraryPhotos")).toArray();

    for (const QJsonValue &value : photos) {
        if (!value.isString()) {
            continue;
        }

        const QString filePath = normalizedPath(value.toString());
        if (!filePath.isEmpty() && !result.contains(filePath)) {
            result.push_back(filePath);
        }
    }

    return result;
}

void ApplicationStateStore::addLibraryPhoto(const QString &filePath)
{
    const QString absoluteFilePath = normalizedPath(filePath);
    if (absoluteFilePath.isEmpty()) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonArray photos = root.value(QStringLiteral("libraryPhotos")).toArray();

    for (const QJsonValue &value : photos) {
        if (value.isString() && normalizedPath(value.toString()) == absoluteFilePath) {
            return;
        }
    }

    photos.append(absoluteFilePath);
    root.insert(QStringLiteral("libraryPhotos"), photos);
    saveStateObject(std::move(root));
}

QHash<QString, int> ApplicationStateStore::loadPhotoRatings()
{
    QHash<QString, int> result;
    const QJsonObject ratings =
        loadStateObject().value(QStringLiteral("photoRatings")).toObject();

    for (auto iterator = ratings.constBegin(); iterator != ratings.constEnd(); ++iterator) {
        const QString filePath = normalizedPath(iterator.key());
        const int rating = iterator.value().toInt(0);
        if (!filePath.isEmpty() && rating >= 1 && rating <= 5) {
            result.insert(filePath, rating);
        }
    }

    return result;
}

void ApplicationStateStore::setPhotoRating(const QString &filePath, int rating)
{
    const QString absoluteFilePath = normalizedPath(filePath);
    if (absoluteFilePath.isEmpty() || rating < 1 || rating > 5) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonObject ratings = root.value(QStringLiteral("photoRatings")).toObject();
    ratings.insert(absoluteFilePath, rating);
    root.insert(QStringLiteral("photoRatings"), ratings);
    saveStateObject(std::move(root));
}

QHash<QString, int> ApplicationStateStore::loadPhotoStatuses()
{
    QHash<QString, int> result;
    const QJsonObject statuses =
        loadStateObject().value(QStringLiteral("photoStatuses")).toObject();

    for (auto iterator = statuses.constBegin(); iterator != statuses.constEnd(); ++iterator) {
        const QString filePath = normalizedPath(iterator.key());
        const int status = iterator.value().toInt(0);
        if (!filePath.isEmpty() && (status == 1 || status == -1)) {
            result.insert(filePath, status);
        }
    }

    return result;
}

void ApplicationStateStore::setPhotoStatus(const QString &filePath, int status)
{
    const QString absoluteFilePath = normalizedPath(filePath);
    if (absoluteFilePath.isEmpty() || (status != 1 && status != -1)) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonObject statuses = root.value(QStringLiteral("photoStatuses")).toObject();
    statuses.insert(absoluteFilePath, status);
    root.insert(QStringLiteral("photoStatuses"), statuses);
    saveStateObject(std::move(root));
}

QVector<HistoryModel::Snapshot> ApplicationStateStore::loadHistories()
{
    QVector<HistoryModel::Snapshot> result;
    const QJsonArray histories =
        loadStateObject().value(QStringLiteral("histories")).toArray();

    for (const QJsonValue &historyValue : histories) {
        if (!historyValue.isObject()) {
            continue;
        }

        const QJsonObject historyObject = historyValue.toObject();
        const QString filePath =
            normalizedPath(historyObject.value(QStringLiteral("filePath")).toString());
        if (filePath.isEmpty()) {
            continue;
        }

        QVector<HistoryModel::Entry> entries;
        const QJsonArray entryArray =
            historyObject.value(QStringLiteral("entries")).toArray();
        entries.reserve(entryArray.size());

        for (const QJsonValue &entryValue : entryArray) {
            if (!entryValue.isObject()) {
                continue;
            }

            const QJsonObject entryObject = entryValue.toObject();
            const QString label = entryObject.value(QStringLiteral("label")).toString();
            const QJsonObject stateObject =
                entryObject.value(QStringLiteral("state")).toObject();
            if (label.isEmpty() || stateObject.isEmpty()) {
                continue;
            }

            entries.push_back({label, developStateFromJson(stateObject)});
        }

        if (entries.isEmpty()) {
            continue;
        }

        const int currentIndex = std::clamp(
            historyObject.value(QStringLiteral("currentIndex")).toInt(0),
            0,
            static_cast<int>(entries.size()) - 1);
        result.push_back({filePath, std::move(entries), currentIndex});
    }

    return result;
}

void ApplicationStateStore::saveHistories(
    const QVector<HistoryModel::Snapshot> &histories)
{
    QJsonArray historyArray;

    for (const HistoryModel::Snapshot &history : histories) {
        if (history.filePath.isEmpty() || history.entries.isEmpty()) {
            continue;
        }

        QJsonArray entries;
        for (const HistoryModel::Entry &entry : history.entries) {
            QJsonObject entryObject;
            entryObject.insert(QStringLiteral("label"), entry.label);
            entryObject.insert(QStringLiteral("state"), developStateToJson(entry.state));
            entries.append(entryObject);
        }

        QJsonObject historyObject;
        historyObject.insert(QStringLiteral("filePath"), normalizedPath(history.filePath));
        historyObject.insert(QStringLiteral("currentIndex"), history.currentIndex);
        historyObject.insert(QStringLiteral("entries"), entries);
        historyArray.append(historyObject);
    }

    QJsonObject root = loadStateObject();
    root.insert(QStringLiteral("histories"), historyArray);
    saveStateObject(std::move(root));
}
