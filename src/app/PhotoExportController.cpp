#include "app/PhotoExportController.h"

#include "app/ExportDialog.h"
#include "app/HistoryModel.h"
#include "imaging/ArwDecoder.h"
#include "imaging/CropTransform.h"
#include "imaging/DetailProcessor.h"
#include "imaging/ImageLoader.h"
#include "imaging/PhotoMetadata.h"
#include "imaging/PhotoMetadataLoader.h"
#include "ui/filmstrip/FilmstripWidget.h"

#include <QAction>
#include <QDateTime>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QKeySequence>
#include <QMessageBox>
#include <QSaveFile>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <optional>

namespace {

struct ExportRenderResult {
    QImage image;
    imaging::PhotoMetadata metadata;
    QString error;
};

bool isArwPath(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("arw"),
                                               Qt::CaseInsensitive) == 0;
}

QString extensionForFormat(ExportDialog::Format format)
{
    return format == ExportDialog::Format::Png
        ? QStringLiteral(".png")
        : QStringLiteral(".jpg");
}

QString filterForFormat(ExportDialog::Format format)
{
    return format == ExportDialog::Format::Png
        ? QStringLiteral("PNG Image (*.png)")
        : QStringLiteral("JPEG Image (*.jpg *.jpeg)");
}

QString exportPathWithFormat(QString filePath, ExportDialog::Format format)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    const bool matchesFormat = format == ExportDialog::Format::Png
        ? suffix == QStringLiteral("png")
        : suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");

    if (matchesFormat) {
        return filePath;
    }

    const QString extension = extensionForFormat(format);
    if (suffix.isEmpty()) {
        return filePath + extension;
    }

    return QDir(info.absolutePath()).filePath(info.completeBaseName() + extension);
}

QString exportDirectoryForSettings(const QString &sourcePath,
                                   const ExportDialog::Settings &settings)
{
    const QFileInfo sourceInfo(sourcePath);
    const QString sourceDirectory = sourceInfo.absolutePath();

    switch (settings.locationMode) {
    case ExportDialog::LocationMode::OriginalSubfolder:
        return QDir::cleanPath(
            QDir(sourceDirectory).filePath(settings.subfolderName.trimmed()));
    case ExportDialog::LocationMode::CustomFolder:
        return QDir::cleanPath(settings.customFolder);
    case ExportDialog::LocationMode::SameAsOriginal:
    default:
        return sourceDirectory;
    }
}

ExportRenderResult renderForExport(const QString &filePath)
{
    ExportRenderResult result;
    HistoryModel history;
    const std::optional<DevelopState> savedState = history.currentState(filePath);

    if (!isArwPath(filePath)) {
        const imaging::ImageLoadResult loaded = imaging::ImageLoader::load(filePath);
        result.image = loaded.image;
        result.metadata = loaded.metadata;
        result.error = loaded.error;
        if (savedState && !result.image.isNull()) {
            result.image = imaging::DetailProcessor::apply(result.image,
                                                           savedState->detail);
            result.image = imaging::applyCropTransform(result.image, *savedState);
        }
        return result;
    }

    if (!savedState) {
        result.image = imaging::ArwDecoder::decode(filePath,
                                                  &result.error,
                                                  &result.metadata);
        return result;
    }

    const DevelopState &state = *savedState;
    result.image = imaging::ArwDecoder::decode(filePath,
                                               &result.error,
                                               &result.metadata,
                                               state.temperatureKelvin,
                                               state.tint,
                                               state.exposureHundredths,
                                               state.highlights,
                                               state.shadows,
                                               state.whites,
                                               state.blacks,
                                               state.contrast,
                                               state.texture,
                                               state.clarity,
                                               state.dehaze,
                                               state.saturation,
                                               state.vibrance,
                                               state.calibration,
                                               false);
    if (!result.image.isNull()) {
        result.image = imaging::DetailProcessor::apply(result.image, state.detail);
        result.image = imaging::applyCropTransform(result.image, state);
    }
    return result;
}

QImage resizeForExport(const QImage &image, const ExportDialog::Settings &settings)
{
    if (image.isNull() || settings.resizeMode == ExportDialog::ResizeMode::Original) {
        return image;
    }

    if (settings.resizeMode == ExportDialog::ResizeMode::LongEdge) {
        const int longEdge = std::max(image.width(), image.height());
        if (longEdge <= 0 || settings.longEdgePixels <= 0
            || longEdge == settings.longEdgePixels) {
            return image;
        }

        return image.scaled(settings.longEdgePixels,
                            settings.longEdgePixels,
                            Qt::KeepAspectRatio,
                            Qt::SmoothTransformation);
    }

    const double scale = settings.resizeMode == ExportDialog::ResizeMode::Percent50
        ? 0.5
        : 0.25;
    const QSize targetSize(
        std::max(1, static_cast<int>(std::lround(image.width() * scale))),
        std::max(1, static_cast<int>(std::lround(image.height() * scale))));
    return image.scaled(targetSize,
                        Qt::KeepAspectRatio,
                        Qt::SmoothTransformation);
}

QString shutterMetadataValue(double seconds)
{
    if (seconds <= 0.0) {
        return {};
    }

    if (seconds < 1.0) {
        const int denominator = std::max(1, static_cast<int>(std::lround(1.0 / seconds)));
        return QStringLiteral("1/%1 s").arg(denominator);
    }

    return QStringLiteral("%1 s").arg(QString::number(seconds, 'g', 6));
}

void addTextIfAvailable(QImageWriter &writer, const QString &key, const QString &value)
{
    if (!value.trimmed().isEmpty()) {
        writer.setText(key, value.trimmed());
    }
}

void addAvailableMetadata(QImageWriter &writer,
                          const imaging::PhotoMetadata &metadata,
                          const ExportDialog::Settings &settings)
{
    if (settings.includeCameraLensInfo) {
        addTextIfAvailable(writer, QStringLiteral("Camera Make"), metadata.cameraMake);
        addTextIfAvailable(writer, QStringLiteral("Camera Model"), metadata.cameraModel);
        addTextIfAvailable(writer, QStringLiteral("Lens Make"), metadata.lensMake);
        addTextIfAvailable(writer, QStringLiteral("Lens Model"), metadata.lensModel);
        if (metadata.focalLengthMm > 0.0) {
            writer.setText(QStringLiteral("Focal Length"),
                           QStringLiteral("%1 mm")
                               .arg(QString::number(metadata.focalLengthMm, 'g', 5)));
        }
    }

    if (settings.includeCaptureSettings) {
        if (metadata.shutterSeconds > 0.0) {
            writer.setText(QStringLiteral("Exposure Time"),
                           shutterMetadataValue(metadata.shutterSeconds));
        }
        if (metadata.aperture > 0.0) {
            writer.setText(QStringLiteral("Aperture"),
                           QStringLiteral("f/%1")
                               .arg(QString::number(metadata.aperture, 'g', 4)));
        }
        if (metadata.iso > 0.0) {
            writer.setText(QStringLiteral("ISO"),
                           QString::number(static_cast<int>(std::lround(metadata.iso))));
        }
        if (metadata.whiteBalanceTemperatureKelvin > 0) {
            writer.setText(QStringLiteral("White Balance Temperature"),
                           QStringLiteral("%1 K")
                               .arg(metadata.whiteBalanceTemperatureKelvin));
        }
    }

    if (settings.includeCaptureDateTime && metadata.captureTimestamp > 0) {
        writer.setText(QStringLiteral("Date/Time Captured"),
                       QDateTime::fromSecsSinceEpoch(metadata.captureTimestamp)
                           .toString(Qt::ISODate));
    }

    if (settings.includeLocationInfo && metadata.hasLocation) {
        writer.setText(QStringLiteral("GPS Latitude"),
                       QString::number(metadata.latitude, 'f', 6));
        writer.setText(QStringLiteral("GPS Longitude"),
                       QString::number(metadata.longitude, 'f', 6));
        if (metadata.hasAltitude) {
            writer.setText(QStringLiteral("GPS Altitude"),
                           QStringLiteral("%1 m")
                               .arg(QString::number(metadata.altitudeMeters, 'f', 2)));
        }
    }

    if (settings.includeCopyrightCreator) {
        addTextIfAvailable(writer, QStringLiteral("Author"), metadata.creator);
        addTextIfAvailable(writer, QStringLiteral("Copyright"), metadata.copyrightNotice);
    }

    if (settings.includeDescriptionKeywords) {
        addTextIfAvailable(writer, QStringLiteral("Description"), metadata.description);
        addTextIfAvailable(writer, QStringLiteral("Keywords"), metadata.keywords);
    }

    if (settings.includeDeviceIdentifiers) {
        addTextIfAvailable(writer,
                           QStringLiteral("Camera Serial Number"),
                           metadata.cameraSerial);
        addTextIfAvailable(writer,
                           QStringLiteral("Lens Serial Number"),
                           metadata.lensSerial);
    }
}

bool writeExportImage(const QImage &image,
                      const QString &filePath,
                      const ExportDialog::Settings &settings,
                      const imaging::PhotoMetadata &metadata,
                      QString *error)
{
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("The rendered image is empty.");
        }
        return false;
    }

    const QByteArray format = settings.format == ExportDialog::Format::Png
        ? QByteArrayLiteral("png")
        : QByteArrayLiteral("jpeg");

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    {
        QImageWriter writer(&file, format);
        if (settings.format == ExportDialog::Format::Jpeg) {
            writer.setQuality(std::clamp(settings.quality, 1, 100));
        }
        addAvailableMetadata(writer, metadata, settings);

        if (!writer.write(image)) {
            if (error) {
                *error = writer.errorString();
            }
            file.cancelWriting();
            return false;
        }
    }

    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    return true;
}

} // namespace

PhotoExportController::PhotoExportController(QWidget *window, QObject *parent)
    : QObject(parent)
    , m_window(window)
{
    if (!m_window) {
        return;
    }

    if (QWidget *filmstripWidget =
            m_window->findChild<QWidget *>(QStringLiteral("filmstripWidget"))) {
        m_filmstrip = static_cast<FilmstripWidget *>(filmstripWidget);
    }

    m_exportAction = new QAction(tr("Export…"), this);
    m_exportAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
    m_window->addAction(m_exportAction);

    connect(m_exportAction, &QAction::triggered,
            this, &PhotoExportController::exportCurrentPhoto);
}

void PhotoExportController::exportCurrentPhoto()
{
    if (!m_window || !m_filmstrip) {
        return;
    }

    const QString sourcePath = m_filmstrip->currentPhotoPath();
    if (sourcePath.isEmpty()) {
        QMessageBox::information(m_window,
                                 tr("Export"),
                                 tr("Open or select a photo before exporting."));
        return;
    }

    const QFileInfo sourceInfo(sourcePath);
    ExportDialog exportDialog(sourceInfo.absolutePath(), m_window);
    if (exportDialog.exec() != QDialog::Accepted) {
        return;
    }
    const ExportDialog::Settings settings = exportDialog.settings();

    const QString exportDirectory = exportDirectoryForSettings(sourcePath, settings);
    if (exportDirectory.isEmpty()) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Choose a valid export location."));
        return;
    }

    if (settings.locationMode == ExportDialog::LocationMode::OriginalSubfolder) {
        if (!QDir().mkpath(exportDirectory)) {
            QMessageBox::critical(m_window,
                                  tr("Export Failed"),
                                  tr("Nexus could not create the export folder:\n%1")
                                      .arg(QDir::toNativeSeparators(exportDirectory)));
            return;
        }
    } else if (!QDir(exportDirectory).exists()) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("The selected export folder no longer exists:\n%1")
                                  .arg(QDir::toNativeSeparators(exportDirectory)));
        return;
    }

    const QString extension = extensionForFormat(settings.format);
    const QString suggestedPath = QDir(exportDirectory).filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_export") + extension);

    QString exportPath = QFileDialog::getSaveFileName(
        m_window,
        tr("Export Photo"),
        suggestedPath,
        filterForFormat(settings.format));

    if (exportPath.isEmpty()) {
        return;
    }

    exportPath = exportPathWithFormat(exportPath, settings.format);

    ExportRenderResult rendered = renderForExport(sourcePath);
    if (rendered.image.isNull()) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Apollo could not render the photo for export.\n\n%1")
                                  .arg(rendered.error));
        return;
    }

    imaging::PhotoMetadata exportMetadata = imaging::PhotoMetadataLoader::load(sourcePath);
    if (exportMetadata.shutterSeconds <= 0.0) {
        exportMetadata.shutterSeconds = rendered.metadata.shutterSeconds;
    }
    if (exportMetadata.aperture <= 0.0) {
        exportMetadata.aperture = rendered.metadata.aperture;
    }
    if (exportMetadata.iso <= 0.0) {
        exportMetadata.iso = rendered.metadata.iso;
    }
    if (exportMetadata.focalLengthMm <= 0.0) {
        exportMetadata.focalLengthMm = rendered.metadata.focalLengthMm;
    }
    if (exportMetadata.whiteBalanceTemperatureKelvin <= 0) {
        exportMetadata.whiteBalanceTemperatureKelvin =
            rendered.metadata.whiteBalanceTemperatureKelvin;
    }

    const QImage outputImage = resizeForExport(rendered.image, settings);
    QString error;
    if (!writeExportImage(outputImage,
                          exportPath,
                          settings,
                          exportMetadata,
                          &error)) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Apollo could not save:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(exportPath), error));
    }
}
