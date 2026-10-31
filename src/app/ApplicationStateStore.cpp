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

constexpr int kStateSchemaVersion = 13;
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

QJsonObject calibrationToJson(const imaging::RawCalibration &calibration)
{
    QJsonObject object;
    object.insert(QStringLiteral("shadowsTint"), calibration.shadowsTint);
    object.insert(QStringLiteral("redPrimaryHue"), calibration.redPrimaryHue);
    object.insert(QStringLiteral("redPrimarySaturation"), calibration.redPrimarySaturation);
    object.insert(QStringLiteral("greenPrimaryHue"), calibration.greenPrimaryHue);
    object.insert(QStringLiteral("greenPrimarySaturation"),
                  calibration.greenPrimarySaturation);
    object.insert(QStringLiteral("bluePrimaryHue"), calibration.bluePrimaryHue);
    object.insert(QStringLiteral("bluePrimarySaturation"),
                  calibration.bluePrimarySaturation);
    return object;
}

imaging::RawCalibration calibrationFromJson(const QJsonObject &object)
{
    imaging::RawCalibration calibration;
    const auto value = [&object](const char *name) {
        return std::clamp(object.value(QString::fromLatin1(name)).toInt(0), -100, 100);
    };

    calibration.shadowsTint = value("shadowsTint");
    calibration.redPrimaryHue = value("redPrimaryHue");
    calibration.redPrimarySaturation = value("redPrimarySaturation");
    calibration.greenPrimaryHue = value("greenPrimaryHue");
    calibration.greenPrimarySaturation = value("greenPrimarySaturation");
    calibration.bluePrimaryHue = value("bluePrimaryHue");
    calibration.bluePrimarySaturation = value("bluePrimarySaturation");
    return calibration;
}

QJsonArray colorMixerValuesToJson(
    const std::array<int, imaging::kColorMixerBandCount> &values)
{
    QJsonArray array;
    for (int value : values) {
        array.append(std::clamp(value, -100, 100));
    }
    return array;
}

std::array<int, imaging::kColorMixerBandCount> colorMixerValuesFromJson(
    const QJsonValue &value)
{
    std::array<int, imaging::kColorMixerBandCount> values{};
    const QJsonArray array = value.toArray();
    const int count = std::min(static_cast<int>(array.size()),
                               static_cast<int>(imaging::kColorMixerBandCount));
    for (int index = 0; index < count; ++index) {
        values[static_cast<std::size_t>(index)] =
            std::clamp(array[index].toInt(0), -100, 100);
    }
    return values;
}

QJsonObject colorMixerToJson(const imaging::ColorMixerSettings &settings)
{
    QJsonObject object;
    object.insert(QStringLiteral("enabled"), settings.enabled);
    object.insert(QStringLiteral("hue"), colorMixerValuesToJson(settings.hue));
    object.insert(QStringLiteral("saturation"),
                  colorMixerValuesToJson(settings.saturation));
    object.insert(QStringLiteral("luminance"),
                  colorMixerValuesToJson(settings.luminance));
    return object;
}

imaging::ColorMixerSettings colorMixerFromJson(const QJsonObject &object)
{
    imaging::ColorMixerSettings settings;
    settings.enabled = object.contains(QStringLiteral("enabled"))
        ? object.value(QStringLiteral("enabled")).toBool(true)
        : true;
    settings.hue = colorMixerValuesFromJson(object.value(QStringLiteral("hue")));
    settings.saturation = colorMixerValuesFromJson(
        object.value(QStringLiteral("saturation")));
    settings.luminance = colorMixerValuesFromJson(
        object.value(QStringLiteral("luminance")));
    return settings;
}

QJsonObject detailToJson(const imaging::DetailSettings &detail)
{
    QJsonObject object;
    object.insert(QStringLiteral("sharpeningAmount"), detail.sharpeningAmount);
    object.insert(QStringLiteral("sharpeningRadiusHundredths"),
                  detail.sharpeningRadiusHundredths);
    object.insert(QStringLiteral("sharpeningDetail"), detail.sharpeningDetail);
    object.insert(QStringLiteral("sharpeningMasking"), detail.sharpeningMasking);
    object.insert(QStringLiteral("luminanceNoiseReduction"),
                  detail.luminanceNoiseReduction);
    object.insert(QStringLiteral("colorNoiseReduction"),
                  detail.colorNoiseReduction);
    return object;
}

imaging::DetailSettings detailFromJson(const QJsonObject &object)
{
    imaging::DetailSettings detail;
    detail.sharpeningAmount = std::clamp(
        object.value(QStringLiteral("sharpeningAmount")).toInt(detail.sharpeningAmount),
        0,
        150);
    detail.sharpeningRadiusHundredths = std::clamp(
        object.value(QStringLiteral("sharpeningRadiusHundredths"))
            .toInt(detail.sharpeningRadiusHundredths),
        50,
        300);
    detail.sharpeningDetail = std::clamp(
        object.value(QStringLiteral("sharpeningDetail")).toInt(detail.sharpeningDetail),
        0,
        100);
    detail.sharpeningMasking = std::clamp(
        object.value(QStringLiteral("sharpeningMasking")).toInt(detail.sharpeningMasking),
        0,
        100);
    detail.luminanceNoiseReduction = std::clamp(
        object.value(QStringLiteral("luminanceNoiseReduction"))
            .toInt(detail.luminanceNoiseReduction),
        0,
        100);
    detail.colorNoiseReduction = std::clamp(
        object.value(QStringLiteral("colorNoiseReduction"))
            .toInt(detail.colorNoiseReduction),
        0,
        100);
    return detail;
}

QJsonObject effectsToJson(const imaging::EffectsSettings &effects)
{
    QJsonObject object;
    object.insert(QStringLiteral("vignetteAmount"), effects.vignetteAmount);
    object.insert(QStringLiteral("vignetteMidpoint"), effects.vignetteMidpoint);
    object.insert(QStringLiteral("vignetteRoundness"), effects.vignetteRoundness);
    object.insert(QStringLiteral("vignetteFeather"), effects.vignetteFeather);
    object.insert(QStringLiteral("vignetteHighlights"), effects.vignetteHighlights);
    object.insert(QStringLiteral("grainAmount"), effects.grainAmount);
    object.insert(QStringLiteral("grainSize"), effects.grainSize);
    object.insert(QStringLiteral("grainRoughness"), effects.grainRoughness);
    return object;
}

imaging::EffectsSettings effectsFromJson(const QJsonObject &object)
{
    imaging::EffectsSettings effects;
    effects.vignetteAmount = std::clamp(
        object.value(QStringLiteral("vignetteAmount")).toInt(effects.vignetteAmount),
        -100,
        100);
    effects.vignetteMidpoint = std::clamp(
        object.value(QStringLiteral("vignetteMidpoint")).toInt(effects.vignetteMidpoint),
        0,
        100);
    effects.vignetteRoundness = std::clamp(
        object.value(QStringLiteral("vignetteRoundness")).toInt(effects.vignetteRoundness),
        -100,
        100);
    effects.vignetteFeather = std::clamp(
        object.value(QStringLiteral("vignetteFeather")).toInt(effects.vignetteFeather),
        0,
        100);
    effects.vignetteHighlights = std::clamp(
        object.value(QStringLiteral("vignetteHighlights")).toInt(effects.vignetteHighlights),
        0,
        100);
    effects.grainAmount = std::clamp(
        object.value(QStringLiteral("grainAmount")).toInt(effects.grainAmount),
        0,
        100);
    effects.grainSize = std::clamp(
        object.value(QStringLiteral("grainSize")).toInt(effects.grainSize),
        0,
        100);
    effects.grainRoughness = std::clamp(
        object.value(QStringLiteral("grainRoughness")).toInt(effects.grainRoughness),
        0,
        100);
    return effects;
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
    object.insert(QStringLiteral("colorMixer"), colorMixerToJson(state.colorMixer));
    object.insert(QStringLiteral("calibration"), calibrationToJson(state.calibration));
    object.insert(QStringLiteral("detail"), detailToJson(state.detail));
    object.insert(QStringLiteral("effects"), effectsToJson(state.effects));
    object.insert(QStringLiteral("cropX"), state.cropX);
    object.insert(QStringLiteral("cropY"), state.cropY);
    object.insert(QStringLiteral("cropWidth"), state.cropWidth);
    object.insert(QStringLiteral("cropHeight"), state.cropHeight);
    object.insert(QStringLiteral("cropQuarterTurns"), state.cropQuarterTurns);
    object.insert(QStringLiteral("cropFlipHorizontal"), state.cropFlipHorizontal);
    object.insert(QStringLiteral("cropFlipVertical"), state.cropFlipVertical);
    object.insert(QStringLiteral("cropStraightenHundredths"),
                  state.cropStraightenHundredths);
    object.insert(QStringLiteral("cropAspectMode"), state.cropAspectMode);
    object.insert(QStringLiteral("transformUprightMode"), state.transformUprightMode);
    object.insert(QStringLiteral("transformUprightVertical"),
                  state.transformUprightVertical);
    object.insert(QStringLiteral("transformUprightHorizontal"),
                  state.transformUprightHorizontal);
    object.insert(QStringLiteral("transformUprightRotateHundredths"),
                  state.transformUprightRotateHundredths);
    object.insert(QStringLiteral("transformUprightAspect"), state.transformUprightAspect);
    object.insert(QStringLiteral("transformVertical"), state.transformVertical);
    object.insert(QStringLiteral("transformHorizontal"), state.transformHorizontal);
    object.insert(QStringLiteral("transformRotateHundredths"),
                  state.transformRotateHundredths);
    object.insert(QStringLiteral("transformAspect"), state.transformAspect);
    object.insert(QStringLiteral("transformScale"), state.transformScale);
    object.insert(QStringLiteral("transformXOffset"), state.transformXOffset);
    object.insert(QStringLiteral("transformYOffset"), state.transformYOffset);
    object.insert(QStringLiteral("transformConstrainCrop"), state.transformConstrainCrop);
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
    state.colorMixer = colorMixerFromJson(
        object.value(QStringLiteral("colorMixer")).toObject());
    state.calibration = calibrationFromJson(
        object.value(QStringLiteral("calibration")).toObject());
    state.detail = detailFromJson(object.value(QStringLiteral("detail")).toObject());
    state.effects = effectsFromJson(object.value(QStringLiteral("effects")).toObject());
    state.cropX = object.value(QStringLiteral("cropX")).toInt(state.cropX);
    state.cropY = object.value(QStringLiteral("cropY")).toInt(state.cropY);
    state.cropWidth = object.value(QStringLiteral("cropWidth")).toInt(state.cropWidth);
    state.cropHeight = object.value(QStringLiteral("cropHeight")).toInt(state.cropHeight);
    state.cropQuarterTurns = object.value(QStringLiteral("cropQuarterTurns"))
                                 .toInt(state.cropQuarterTurns);
    state.cropFlipHorizontal = object.value(QStringLiteral("cropFlipHorizontal"))
                                   .toBool(state.cropFlipHorizontal);
    state.cropFlipVertical = object.value(QStringLiteral("cropFlipVertical"))
                                 .toBool(state.cropFlipVertical);
    state.cropStraightenHundredths =
        object.value(QStringLiteral("cropStraightenHundredths"))
            .toInt(state.cropStraightenHundredths);
    state.cropAspectMode = object.value(QStringLiteral("cropAspectMode"))
                               .toInt(state.cropAspectMode);
    state.transformUprightMode = std::clamp(
        object.value(QStringLiteral("transformUprightMode")).toInt(state.transformUprightMode),
        0, 5);

    const bool hasSeparatedUpright =
        object.contains(QStringLiteral("transformUprightVertical"))
        || object.contains(QStringLiteral("transformUprightHorizontal"))
        || object.contains(QStringLiteral("transformUprightRotateHundredths"))
        || object.contains(QStringLiteral("transformUprightAspect"));

    state.transformUprightVertical = std::clamp(
        object.value(QStringLiteral("transformUprightVertical"))
            .toInt(state.transformUprightVertical),
        -100, 100);
    state.transformUprightHorizontal = std::clamp(
        object.value(QStringLiteral("transformUprightHorizontal"))
            .toInt(state.transformUprightHorizontal),
        -100, 100);
    state.transformUprightRotateHundredths = std::clamp(
        object.value(QStringLiteral("transformUprightRotateHundredths"))
            .toInt(state.transformUprightRotateHundredths),
        -1000, 1000);
    state.transformUprightAspect = std::clamp(
        object.value(QStringLiteral("transformUprightAspect"))
            .toInt(state.transformUprightAspect),
        -100, 100);

    state.transformVertical = std::clamp(
        object.value(QStringLiteral("transformVertical")).toInt(state.transformVertical),
        -100, 100);
    state.transformHorizontal = std::clamp(
        object.value(QStringLiteral("transformHorizontal")).toInt(state.transformHorizontal),
        -100, 100);
    state.transformRotateHundredths = std::clamp(
        object.value(QStringLiteral("transformRotateHundredths"))
            .toInt(state.transformRotateHundredths),
        -1000, 1000);
    state.transformAspect = std::clamp(
        object.value(QStringLiteral("transformAspect")).toInt(state.transformAspect),
        -100, 100);
    state.transformScale = std::clamp(
        object.value(QStringLiteral("transformScale")).toInt(state.transformScale),
        50, 150);
    state.transformXOffset = std::clamp(
        object.value(QStringLiteral("transformXOffset")).toInt(state.transformXOffset),
        -100, 100);
    state.transformYOffset = std::clamp(
        object.value(QStringLiteral("transformYOffset")).toInt(state.transformYOffset),
        -100, 100);
    state.transformConstrainCrop =
        object.value(QStringLiteral("transformConstrainCrop"))
            .toBool(state.transformConstrainCrop);

    // Schema 9 stored the Upright solver output directly in the manual sliders.
    // Move those values into the separated Upright stage so existing edits keep
    // the same appearance while the sliders become true post-Upright refinement.
    if (!hasSeparatedUpright && state.transformUprightMode != 0) {
        state.transformUprightVertical = state.transformVertical;
        state.transformUprightHorizontal = state.transformHorizontal;
        state.transformUprightRotateHundredths = state.transformRotateHundredths;
        state.transformVertical = 0;
        state.transformHorizontal = 0;
        state.transformRotateHundredths = 0;
    }

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

void ApplicationStateStore::removeLibraryPhoto(const QString &filePath)
{
    const QString absoluteFilePath = normalizedPath(filePath);
    if (absoluteFilePath.isEmpty()) {
        return;
    }

    QJsonObject root = loadStateObject();
    const QJsonArray photos = root.value(QStringLiteral("libraryPhotos")).toArray();
    QJsonArray remainingPhotos;

    for (const QJsonValue &value : photos) {
        if (!value.isString()
            || normalizedPath(value.toString()) != absoluteFilePath) {
            remainingPhotos.append(value);
        }
    }

    root.insert(QStringLiteral("libraryPhotos"), remainingPhotos);
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
    if (absoluteFilePath.isEmpty() || rating < 0 || rating > 5) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonObject ratings = root.value(QStringLiteral("photoRatings")).toObject();
    if (rating == 0) {
        ratings.remove(absoluteFilePath);
    } else {
        ratings.insert(absoluteFilePath, rating);
    }
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
    if (absoluteFilePath.isEmpty()
        || (status != 0 && status != 1 && status != -1)) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonObject statuses = root.value(QStringLiteral("photoStatuses")).toObject();
    if (status == 0) {
        statuses.remove(absoluteFilePath);
    } else {
        statuses.insert(absoluteFilePath, status);
    }
    root.insert(QStringLiteral("photoStatuses"), statuses);
    saveStateObject(std::move(root));
}

QHash<QString, int> ApplicationStateStore::loadPhotoColorLabels()
{
    QHash<QString, int> result;
    const QJsonObject colorLabels =
        loadStateObject().value(QStringLiteral("photoColorLabels")).toObject();

    for (auto iterator = colorLabels.constBegin(); iterator != colorLabels.constEnd(); ++iterator) {
        const QString filePath = normalizedPath(iterator.key());
        const int colorLabel = iterator.value().toInt(0);
        if (!filePath.isEmpty() && colorLabel >= 1 && colorLabel <= 5) {
            result.insert(filePath, colorLabel);
        }
    }

    return result;
}

void ApplicationStateStore::setPhotoColorLabel(const QString &filePath, int colorLabel)
{
    const QString absoluteFilePath = normalizedPath(filePath);
    if (absoluteFilePath.isEmpty() || colorLabel < 0 || colorLabel > 5) {
        return;
    }

    QJsonObject root = loadStateObject();
    QJsonObject colorLabels = root.value(QStringLiteral("photoColorLabels")).toObject();
    if (colorLabel == 0) {
        colorLabels.remove(absoluteFilePath);
    } else {
        colorLabels.insert(absoluteFilePath, colorLabel);
    }
    root.insert(QStringLiteral("photoColorLabels"), colorLabels);
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
