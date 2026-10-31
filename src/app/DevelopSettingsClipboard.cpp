#include "app/DevelopSettingsClipboard.h"

#include "app/ApplicationStateStore.h"
#include "app/MainWindow.h"
#include "imaging/TemperatureRenderService.h"

#include <QClipboard>
#include <QFileInfo>
#include <QGuiApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QObject>
#include <QWidget>

#include <algorithm>
#include <optional>

namespace {

constexpr int kClipboardVersion = 3;
constexpr int kMinimumSupportedClipboardVersion = 2;
constexpr auto kClipboardFormat = "apollo-develop-settings";

std::optional<DevelopState> currentStateForPhoto(const QString &filePath)
{
    if (filePath.isEmpty()) {
        return std::nullopt;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    const QVector<HistoryModel::Snapshot> histories =
        ApplicationStateStore::loadHistories();

    for (const HistoryModel::Snapshot &snapshot : histories) {
        if (QFileInfo(snapshot.filePath).absoluteFilePath() != absoluteFilePath
            || snapshot.currentIndex < 0
            || snapshot.currentIndex >= snapshot.entries.size()) {
            continue;
        }
        return snapshot.entries.at(snapshot.currentIndex).state;
    }

    return std::nullopt;
}

QJsonObject calibrationToJson(const imaging::RawCalibration &calibration)
{
    QJsonObject object;
    object.insert(QStringLiteral("shadowsTint"), calibration.shadowsTint);
    object.insert(QStringLiteral("redPrimaryHue"), calibration.redPrimaryHue);
    object.insert(QStringLiteral("redPrimarySaturation"), calibration.redPrimarySaturation);
    object.insert(QStringLiteral("greenPrimaryHue"), calibration.greenPrimaryHue);
    object.insert(QStringLiteral("greenPrimarySaturation"), calibration.greenPrimarySaturation);
    object.insert(QStringLiteral("bluePrimaryHue"), calibration.bluePrimaryHue);
    object.insert(QStringLiteral("bluePrimarySaturation"), calibration.bluePrimarySaturation);
    return object;
}

imaging::RawCalibration calibrationFromJson(const QJsonObject &object)
{
    imaging::RawCalibration calibration;
    calibration.shadowsTint = object.value(QStringLiteral("shadowsTint")).toInt();
    calibration.redPrimaryHue = object.value(QStringLiteral("redPrimaryHue")).toInt();
    calibration.redPrimarySaturation = object.value(QStringLiteral("redPrimarySaturation")).toInt();
    calibration.greenPrimaryHue = object.value(QStringLiteral("greenPrimaryHue")).toInt();
    calibration.greenPrimarySaturation = object.value(QStringLiteral("greenPrimarySaturation")).toInt();
    calibration.bluePrimaryHue = object.value(QStringLiteral("bluePrimaryHue")).toInt();
    calibration.bluePrimarySaturation = object.value(QStringLiteral("bluePrimarySaturation")).toInt();
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
    object.insert(QStringLiteral("sharpeningRadiusHundredths"), detail.sharpeningRadiusHundredths);
    object.insert(QStringLiteral("sharpeningDetail"), detail.sharpeningDetail);
    object.insert(QStringLiteral("sharpeningMasking"), detail.sharpeningMasking);
    object.insert(QStringLiteral("luminanceNoiseReduction"), detail.luminanceNoiseReduction);
    object.insert(QStringLiteral("colorNoiseReduction"), detail.colorNoiseReduction);
    return object;
}

imaging::DetailSettings detailFromJson(const QJsonObject &object)
{
    imaging::DetailSettings detail;
    detail.sharpeningAmount = object.value(QStringLiteral("sharpeningAmount"))
                                  .toInt(detail.sharpeningAmount);
    detail.sharpeningRadiusHundredths =
        object.value(QStringLiteral("sharpeningRadiusHundredths"))
            .toInt(detail.sharpeningRadiusHundredths);
    detail.sharpeningDetail = object.value(QStringLiteral("sharpeningDetail"))
                                  .toInt(detail.sharpeningDetail);
    detail.sharpeningMasking = object.value(QStringLiteral("sharpeningMasking"))
                                   .toInt(detail.sharpeningMasking);
    detail.luminanceNoiseReduction =
        object.value(QStringLiteral("luminanceNoiseReduction"))
            .toInt(detail.luminanceNoiseReduction);
    detail.colorNoiseReduction = object.value(QStringLiteral("colorNoiseReduction"))
                                     .toInt(detail.colorNoiseReduction);
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
    effects.vignetteAmount = object.value(QStringLiteral("vignetteAmount"))
                                 .toInt(effects.vignetteAmount);
    effects.vignetteMidpoint = object.value(QStringLiteral("vignetteMidpoint"))
                                   .toInt(effects.vignetteMidpoint);
    effects.vignetteRoundness = object.value(QStringLiteral("vignetteRoundness"))
                                    .toInt(effects.vignetteRoundness);
    effects.vignetteFeather = object.value(QStringLiteral("vignetteFeather"))
                                  .toInt(effects.vignetteFeather);
    effects.vignetteHighlights = object.value(QStringLiteral("vignetteHighlights"))
                                     .toInt(effects.vignetteHighlights);
    effects.grainAmount = object.value(QStringLiteral("grainAmount"))
                              .toInt(effects.grainAmount);
    effects.grainSize = object.value(QStringLiteral("grainSize"))
                            .toInt(effects.grainSize);
    effects.grainRoughness = object.value(QStringLiteral("grainRoughness"))
                                 .toInt(effects.grainRoughness);
    return effects;
}

QJsonObject stateToJson(const DevelopState &state)
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
    object.insert(QStringLiteral("cropStraightenHundredths"), state.cropStraightenHundredths);
    object.insert(QStringLiteral("cropAspectMode"), state.cropAspectMode);
    object.insert(QStringLiteral("transformUprightMode"), state.transformUprightMode);
    object.insert(QStringLiteral("transformUprightVertical"), state.transformUprightVertical);
    object.insert(QStringLiteral("transformUprightHorizontal"), state.transformUprightHorizontal);
    object.insert(QStringLiteral("transformUprightRotateHundredths"),
                  state.transformUprightRotateHundredths);
    object.insert(QStringLiteral("transformUprightAspect"), state.transformUprightAspect);
    object.insert(QStringLiteral("transformVertical"), state.transformVertical);
    object.insert(QStringLiteral("transformHorizontal"), state.transformHorizontal);
    object.insert(QStringLiteral("transformRotateHundredths"), state.transformRotateHundredths);
    object.insert(QStringLiteral("transformAspect"), state.transformAspect);
    object.insert(QStringLiteral("transformScale"), state.transformScale);
    object.insert(QStringLiteral("transformXOffset"), state.transformXOffset);
    object.insert(QStringLiteral("transformYOffset"), state.transformYOffset);
    object.insert(QStringLiteral("transformConstrainCrop"), state.transformConstrainCrop);
    return object;
}

DevelopState stateFromJson(const QJsonObject &object)
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
    state.transformUprightMode = object.value(QStringLiteral("transformUprightMode"))
                                     .toInt(state.transformUprightMode);
    state.transformUprightVertical = object.value(QStringLiteral("transformUprightVertical"))
                                         .toInt(state.transformUprightVertical);
    state.transformUprightHorizontal = object.value(QStringLiteral("transformUprightHorizontal"))
                                           .toInt(state.transformUprightHorizontal);
    state.transformUprightRotateHundredths =
        object.value(QStringLiteral("transformUprightRotateHundredths"))
            .toInt(state.transformUprightRotateHundredths);
    state.transformUprightAspect = object.value(QStringLiteral("transformUprightAspect"))
                                       .toInt(state.transformUprightAspect);
    state.transformVertical = object.value(QStringLiteral("transformVertical"))
                                  .toInt(state.transformVertical);
    state.transformHorizontal = object.value(QStringLiteral("transformHorizontal"))
                                    .toInt(state.transformHorizontal);
    state.transformRotateHundredths =
        object.value(QStringLiteral("transformRotateHundredths"))
            .toInt(state.transformRotateHundredths);
    state.transformAspect = object.value(QStringLiteral("transformAspect"))
                                .toInt(state.transformAspect);
    state.transformScale = object.value(QStringLiteral("transformScale"))
                               .toInt(state.transformScale);
    state.transformXOffset = object.value(QStringLiteral("transformXOffset"))
                                 .toInt(state.transformXOffset);
    state.transformYOffset = object.value(QStringLiteral("transformYOffset"))
                                 .toInt(state.transformYOffset);
    state.transformConstrainCrop = object.value(QStringLiteral("transformConstrainCrop"))
                                       .toBool(state.transformConstrainCrop);
    return state;
}

std::optional<DevelopState> stateFromClipboard()
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    if (!clipboard) {
        return std::nullopt;
    }

    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(
        clipboard->text().toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }

    const QJsonObject payload = document.object();
    const int version = payload.value(QStringLiteral("version")).toInt();
    if (payload.value(QStringLiteral("format")).toString()
            != QString::fromLatin1(kClipboardFormat)
        || version < kMinimumSupportedClipboardVersion
        || version > kClipboardVersion) {
        return std::nullopt;
    }

    const QJsonValue settingsValue = payload.value(QStringLiteral("settings"));
    if (!settingsValue.isObject()) {
        return std::nullopt;
    }

    return stateFromJson(settingsValue.toObject());
}

} // namespace

bool DevelopSettingsClipboard::copyFromPhoto(const QString &filePath)
{
    QClipboard *clipboard = QGuiApplication::clipboard();
    const std::optional<DevelopState> state = currentStateForPhoto(filePath);
    if (!clipboard || !state.has_value()) {
        return false;
    }

    QJsonObject payload;
    payload.insert(QStringLiteral("format"), QString::fromLatin1(kClipboardFormat));
    payload.insert(QStringLiteral("version"), kClipboardVersion);
    payload.insert(QStringLiteral("settings"), stateToJson(*state));
    clipboard->setText(QString::fromUtf8(
        QJsonDocument(payload).toJson(QJsonDocument::Compact)));
    return true;
}

bool DevelopSettingsClipboard::canPaste()
{
    return stateFromClipboard().has_value();
}

bool DevelopSettingsClipboard::pasteToPhoto(QWidget *windowRoot,
                                             const QString &filePath)
{
    const std::optional<DevelopState> copiedState = stateFromClipboard();
    if (!copiedState.has_value() || !windowRoot || filePath.isEmpty()) {
        return false;
    }

    auto *window = qobject_cast<MainWindow *>(windowRoot->window());
    if (!window) {
        return false;
    }

    const QString absoluteFilePath = QFileInfo(filePath).absoluteFilePath();
    if (window->m_currentFilePath != absoluteFilePath) {
        window->openImage(absoluteFilePath);
    }
    if (window->m_currentFilePath != absoluteFilePath) {
        return false;
    }

    if (window->m_renderService) {
        window->m_renderService->setDetailSettings(absoluteFilePath,
                                                    copiedState->detail);
    }

    window->requestFullRender(*copiedState, QObject::tr("Paste Edit Settings"));
    window->setDevelopControlsFromState(*copiedState);
    return true;
}
