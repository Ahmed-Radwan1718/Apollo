#include "app/DevelopSettingsSync.h"

#include "app/MainWindow.h"
#include "imaging/PhotoMetadataLoader.h"

#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

#include <algorithm>

namespace {

constexpr int kDefaultTemperatureKelvin = 6500;

struct SyncSelection {
    bool whiteBalance = true;
    bool tone = true;
    bool presence = true;
    bool color = true;
    bool calibration = true;
    bool detail = true;
    bool crop = true;
    bool transform = true;
    bool effects = true;

    [[nodiscard]] bool any() const noexcept
    {
        return whiteBalance || tone || presence || color || calibration
            || detail || crop || transform || effects;
    }
};

class SyncSettingsDialog final : public QDialog
{
public:
    SyncSettingsDialog(const QString &sourceFilePath,
                       int targetCount,
                       QWidget *parent)
        : QDialog(parent)
    {
        setWindowTitle(tr("Sync Settings"));
        setModal(true);
        setMinimumWidth(340);

        auto *layout = new QVBoxLayout(this);
        layout->setContentsMargins(18, 16, 18, 16);
        layout->setSpacing(10);

        auto *description = new QLabel(
            tr("Synchronize settings from %1 to %2 selected %3.")
                .arg(QFileInfo(sourceFilePath).fileName())
                .arg(targetCount)
                .arg(targetCount == 1 ? tr("photo") : tr("photos")),
            this);
        description->setWordWrap(true);
        layout->addWidget(description);

        m_whiteBalance = addOption(layout, tr("White Balance"));
        m_tone = addOption(layout, tr("Basic Tone"));
        m_presence = addOption(layout, tr("Presence"));
        m_color = addOption(layout, tr("Color"));
        m_calibration = addOption(layout, tr("Calibration"));
        m_detail = addOption(layout, tr("Detail"));
        m_crop = addOption(layout, tr("Crop"));
        m_transform = addOption(layout, tr("Transform"));
        m_effects = addOption(layout, tr("Effects"));

        auto *selectionButtons = new QHBoxLayout;
        selectionButtons->setContentsMargins(0, 4, 0, 0);
        selectionButtons->setSpacing(8);
        auto *checkAll = new QPushButton(tr("Check All"), this);
        auto *checkNone = new QPushButton(tr("Check None"), this);
        selectionButtons->addWidget(checkAll);
        selectionButtons->addWidget(checkNone);
        selectionButtons->addStretch(1);
        layout->addLayout(selectionButtons);

        connect(checkAll, &QPushButton::clicked, this,
                [this]() { setAllChecked(true); });
        connect(checkNone, &QPushButton::clicked, this,
                [this]() { setAllChecked(false); });

        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok
                                                | QDialogButtonBox::Cancel,
                                            this);
        buttons->button(QDialogButtonBox::Ok)->setText(tr("Synchronize"));
        connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
        layout->addWidget(buttons);
    }

    [[nodiscard]] SyncSelection selection() const
    {
        SyncSelection result;
        result.whiteBalance = m_whiteBalance->isChecked();
        result.tone = m_tone->isChecked();
        result.presence = m_presence->isChecked();
        result.color = m_color->isChecked();
        result.calibration = m_calibration->isChecked();
        result.detail = m_detail->isChecked();
        result.crop = m_crop->isChecked();
        result.transform = m_transform->isChecked();
        result.effects = m_effects->isChecked();
        return result;
    }

private:
    QCheckBox *addOption(QVBoxLayout *layout, const QString &text)
    {
        auto *checkBox = new QCheckBox(text, this);
        checkBox->setChecked(true);
        layout->addWidget(checkBox);
        return checkBox;
    }

    void setAllChecked(bool checked)
    {
        for (QCheckBox *checkBox : {m_whiteBalance,
                                    m_tone,
                                    m_presence,
                                    m_color,
                                    m_calibration,
                                    m_detail,
                                    m_crop,
                                    m_transform,
                                    m_effects}) {
            if (checkBox) {
                checkBox->setChecked(checked);
            }
        }
    }

    QCheckBox *m_whiteBalance = nullptr;
    QCheckBox *m_tone = nullptr;
    QCheckBox *m_presence = nullptr;
    QCheckBox *m_color = nullptr;
    QCheckBox *m_calibration = nullptr;
    QCheckBox *m_detail = nullptr;
    QCheckBox *m_crop = nullptr;
    QCheckBox *m_transform = nullptr;
    QCheckBox *m_effects = nullptr;
};

void applySelection(DevelopState &target,
                    const DevelopState &source,
                    const SyncSelection &selection)
{
    if (selection.whiteBalance) {
        target.temperatureKelvin = source.temperatureKelvin;
        target.tint = source.tint;
    }

    if (selection.tone) {
        target.exposureHundredths = source.exposureHundredths;
        target.highlights = source.highlights;
        target.shadows = source.shadows;
        target.whites = source.whites;
        target.blacks = source.blacks;
        target.contrast = source.contrast;
    }

    if (selection.presence) {
        target.texture = source.texture;
        target.clarity = source.clarity;
        target.dehaze = source.dehaze;
    }

    if (selection.color) {
        target.saturation = source.saturation;
        target.vibrance = source.vibrance;
        target.colorMixer = source.colorMixer;
    }

    if (selection.calibration) {
        target.calibration = source.calibration;
    }

    if (selection.detail) {
        target.detail = source.detail;
    }

    if (selection.crop) {
        target.cropX = source.cropX;
        target.cropY = source.cropY;
        target.cropWidth = source.cropWidth;
        target.cropHeight = source.cropHeight;
        target.cropQuarterTurns = source.cropQuarterTurns;
        target.cropFlipHorizontal = source.cropFlipHorizontal;
        target.cropFlipVertical = source.cropFlipVertical;
        target.cropStraightenHundredths = source.cropStraightenHundredths;
        target.cropAspectMode = source.cropAspectMode;
    }

    if (selection.transform) {
        target.transformUprightMode = source.transformUprightMode;
        target.transformUprightVertical = source.transformUprightVertical;
        target.transformUprightHorizontal = source.transformUprightHorizontal;
        target.transformUprightRotateHundredths = source.transformUprightRotateHundredths;
        target.transformUprightAspect = source.transformUprightAspect;
        target.transformVertical = source.transformVertical;
        target.transformHorizontal = source.transformHorizontal;
        target.transformRotateHundredths = source.transformRotateHundredths;
        target.transformAspect = source.transformAspect;
        target.transformScale = source.transformScale;
        target.transformXOffset = source.transformXOffset;
        target.transformYOffset = source.transformYOffset;
        target.transformConstrainCrop = source.transformConstrainCrop;
    }

    if (selection.effects) {
        target.effects = source.effects;
    }
}

DevelopState originalStateForPhoto(const QString &filePath)
{
    DevelopState original;
    const imaging::PhotoMetadata metadata = imaging::PhotoMetadataLoader::load(filePath);
    original.temperatureKelvin = metadata.whiteBalanceTemperatureKelvin > 0
        ? metadata.whiteBalanceTemperatureKelvin
        : kDefaultTemperatureKelvin;
    return original;
}

} // namespace

bool DevelopSettingsSync::run(QWidget *windowRoot,
                              const QString &sourceFilePath,
                              const QStringList &selectedFilePaths)
{
    if (!windowRoot || sourceFilePath.isEmpty() || selectedFilePaths.size() < 2) {
        return false;
    }

    auto *window = qobject_cast<MainWindow *>(windowRoot->window());
    if (!window) {
        return false;
    }

    const QString sourcePath = QFileInfo(sourceFilePath).absoluteFilePath();
    const std::optional<DevelopState> sourceState =
        window->m_historyModel.currentState(sourcePath);
    if (!sourceState.has_value()) {
        return false;
    }

    QStringList targets;
    for (const QString &filePath : selectedFilePaths) {
        const QString absolutePath = QFileInfo(filePath).absoluteFilePath();
        if (absolutePath.isEmpty() || absolutePath == sourcePath
            || targets.contains(absolutePath)) {
            continue;
        }
        targets.push_back(absolutePath);
    }
    if (targets.isEmpty()) {
        return false;
    }

    SyncSettingsDialog dialog(sourcePath, targets.size(), window);
    if (dialog.exec() != QDialog::Accepted) {
        return false;
    }

    const SyncSelection selection = dialog.selection();
    if (!selection.any()) {
        return false;
    }

    bool changed = false;
    for (const QString &targetPath : targets) {
        if (!window->m_historyModel.hasPhoto(targetPath)) {
            window->m_historyModel.initializePhoto(targetPath,
                                                   originalStateForPhoto(targetPath));
        }

        const std::optional<DevelopState> currentState =
            window->m_historyModel.currentState(targetPath);
        if (!currentState.has_value()) {
            continue;
        }

        DevelopState syncedState = *currentState;
        applySelection(syncedState, *sourceState, selection);
        changed = window->m_historyModel.commit(targetPath,
                                                QObject::tr("Sync Settings"),
                                                syncedState)
            || changed;
    }

    return changed;
}
