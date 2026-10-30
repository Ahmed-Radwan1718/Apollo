#include "app/PhotoExportController.h"

#include "app/ExportDialog.h"
#include "app/HistoryModel.h"
#include "imaging/ArwDecoder.h"
#include "imaging/AvifEncoder.h"
#include "imaging/CropTransform.h"
#include "imaging/DetailProcessor.h"
#include "imaging/ImageLoader.h"
#include "imaging/PhotoMetadata.h"
#include "imaging/PhotoMetadataLoader.h"
#include "ui/filmstrip/FilmstripWidget.h"

#include <QAction>
#include <QColorSpace>
#include <QDateTime>
#include <QDir>
#include <QFile>
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

QString extensionForFormat(ExportDialog::Format format, const QString &sourcePath)
{
    switch (format) {
    case ExportDialog::Format::Png:
        return QStringLiteral(".png");
    case ExportDialog::Format::Tiff:
        return QStringLiteral(".tif");
    case ExportDialog::Format::WebP:
        return QStringLiteral(".webp");
    case ExportDialog::Format::Avif:
        return QStringLiteral(".avif");
    case ExportDialog::Format::Original: {
        const QString suffix = QFileInfo(sourcePath).suffix();
        return suffix.isEmpty() ? QString() : QStringLiteral(".") + suffix;
    }
    case ExportDialog::Format::Jpeg:
    default:
        return QStringLiteral(".jpg");
    }
}

QString filterForFormat(ExportDialog::Format format, const QString &sourcePath)
{
    switch (format) {
    case ExportDialog::Format::Png:
        return QStringLiteral("PNG Image (*.png)");
    case ExportDialog::Format::Tiff:
        return QStringLiteral("TIFF Image (*.tif *.tiff)");
    case ExportDialog::Format::WebP:
        return QStringLiteral("WebP Image (*.webp)");
    case ExportDialog::Format::Avif:
        return QStringLiteral("AVIF Image (*.avif)");
    case ExportDialog::Format::Original: {
        const QString suffix = QFileInfo(sourcePath).suffix();
        if (suffix.isEmpty()) {
            return QStringLiteral("Original File (*)");
        }
        return QStringLiteral("Original %1 File (*.%2)")
            .arg(suffix.toUpper(), suffix.toLower());
    }
    case ExportDialog::Format::Jpeg:
    default:
        return QStringLiteral("JPEG Image (*.jpg *.jpeg)");
    }
}

bool suffixMatchesFormat(const QString &suffix,
                         ExportDialog::Format format,
                         const QString &sourcePath)
{
    switch (format) {
    case ExportDialog::Format::Png:
        return suffix == QStringLiteral("png");
    case ExportDialog::Format::Tiff:
        return suffix == QStringLiteral("tif") || suffix == QStringLiteral("tiff");
    case ExportDialog::Format::WebP:
        return suffix == QStringLiteral("webp");
    case ExportDialog::Format::Avif:
        return suffix == QStringLiteral("avif");
    case ExportDialog::Format::Original:
        return suffix == QFileInfo(sourcePath).suffix().toLower();
    case ExportDialog::Format::Jpeg:
    default:
        return suffix == QStringLiteral("jpg") || suffix == QStringLiteral("jpeg");
    }
}

QString exportPathWithFormat(QString filePath,
                             ExportDialog::Format format,
                             const QString &sourcePath)
{
    const QFileInfo info(filePath);
    const QString suffix = info.suffix().toLower();
    if (suffixMatchesFormat(suffix, format, sourcePath)) {
        return filePath;
    }

    const QString extension = extensionForFormat(format, sourcePath);
    if (extension.isEmpty()) {
        return filePath;
    }
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

bool pathsReferToSameFile(const QString &first, const QString &second)
{
    const QString firstPath = QDir::cleanPath(QFileInfo(first).absoluteFilePath());
    const QString secondPath = QDir::cleanPath(QFileInfo(second).absoluteFilePath());
#ifdef Q_OS_WIN
    return firstPath.compare(secondPath, Qt::CaseInsensitive) == 0;
#else
    return firstPath == secondPath;
#endif
}

bool copyOriginalFile(const QString &sourcePath,
                      const QString &destinationPath,
                      QString *error)
{
    if (pathsReferToSameFile(sourcePath, destinationPath)) {
        if (error) {
            *error = QStringLiteral("Choose a different file name so the source photo is not overwritten.");
        }
        return false;
    }

    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        if (error) {
            *error = source.errorString();
        }
        return false;
    }

    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = destination.errorString();
        }
        return false;
    }

    QByteArray buffer(1024 * 1024, '\0');
    while (!source.atEnd()) {
        const qint64 bytesRead = source.read(buffer.data(), buffer.size());
        if (bytesRead < 0) {
            if (error) {
                *error = source.errorString();
            }
            destination.cancelWriting();
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        if (destination.write(buffer.constData(), bytesRead) != bytesRead) {
            if (error) {
                *error = destination.errorString();
            }
            destination.cancelWriting();
            return false;
        }
    }

    if (!destination.commit()) {
        if (error) {
            *error = destination.errorString();
        }
        return false;
    }

    return true;
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

QColorSpace outputColorSpace(ExportDialog::ColorSpace colorSpace)
{
    switch (colorSpace) {
    case ExportDialog::ColorSpace::DisplayP3:
        return QColorSpace(QColorSpace::DisplayP3);
    case ExportDialog::ColorSpace::AdobeRgb:
        return QColorSpace(QColorSpace::AdobeRgb);
    case ExportDialog::ColorSpace::SRgb:
    default:
        return QColorSpace(QColorSpace::SRgb);
    }
}

QImage convertToOutputColorSpace(const QImage &image,
                                 const ExportDialog::Settings &settings,
                                 QString *error)
{
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("The rendered image is empty.");
        }
        return {};
    }

    QImage source = image;
    if (!source.colorSpace().isValid()) {
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
    }

    const QColorSpace target = outputColorSpace(settings.colorSpace);
    if (source.colorSpace() == target) {
        source.setColorSpace(target);
        return source;
    }

    QImage converted = source.convertedToColorSpace(target);
    if (converted.isNull()) {
        if (error) {
            *error = QStringLiteral("Apollo could not convert the image to the selected color space.");
        }
        return {};
    }

    return converted;
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

void addXmpElement(QString &xmp, const QString &name, const QString &value)
{
    const QString trimmed = value.trimmed();
    if (trimmed.isEmpty()) {
        return;
    }

    xmp += QStringLiteral("<apollo:%1>%2</apollo:%1>")
        .arg(name, trimmed.toHtmlEscaped());
}

QByteArray avifXmpForMetadata(const imaging::PhotoMetadata &metadata,
                              const ExportDialog::Settings &settings)
{
    QString body;

    if (settings.includeCameraLensInfo) {
        addXmpElement(body, QStringLiteral("CameraMake"), metadata.cameraMake);
        addXmpElement(body, QStringLiteral("CameraModel"), metadata.cameraModel);
        addXmpElement(body, QStringLiteral("LensMake"), metadata.lensMake);
        addXmpElement(body, QStringLiteral("LensModel"), metadata.lensModel);
        if (metadata.focalLengthMm > 0.0) {
            addXmpElement(body,
                          QStringLiteral("FocalLength"),
                          QStringLiteral("%1 mm")
                              .arg(QString::number(metadata.focalLengthMm, 'g', 5)));
        }
    }

    if (settings.includeCaptureSettings) {
        if (metadata.shutterSeconds > 0.0) {
            addXmpElement(body,
                          QStringLiteral("ExposureTime"),
                          shutterMetadataValue(metadata.shutterSeconds));
        }
        if (metadata.aperture > 0.0) {
            addXmpElement(body,
                          QStringLiteral("Aperture"),
                          QStringLiteral("f/%1")
                              .arg(QString::number(metadata.aperture, 'g', 4)));
        }
        if (metadata.iso > 0.0) {
            addXmpElement(body,
                          QStringLiteral("ISO"),
                          QString::number(static_cast<int>(std::lround(metadata.iso))));
        }
        if (metadata.whiteBalanceTemperatureKelvin > 0) {
            addXmpElement(body,
                          QStringLiteral("WhiteBalanceTemperature"),
                          QStringLiteral("%1 K")
                              .arg(metadata.whiteBalanceTemperatureKelvin));
        }
    }

    if (settings.includeCaptureDateTime && metadata.captureTimestamp > 0) {
        addXmpElement(body,
                      QStringLiteral("DateTimeCaptured"),
                      QDateTime::fromSecsSinceEpoch(metadata.captureTimestamp)
                          .toString(Qt::ISODate));
    }

    if (settings.includeLocationInfo && metadata.hasLocation) {
        addXmpElement(body,
                      QStringLiteral("GPSLatitude"),
                      QString::number(metadata.latitude, 'f', 6));
        addXmpElement(body,
                      QStringLiteral("GPSLongitude"),
                      QString::number(metadata.longitude, 'f', 6));
        if (metadata.hasAltitude) {
            addXmpElement(body,
                          QStringLiteral("GPSAltitude"),
                          QStringLiteral("%1 m")
                              .arg(QString::number(metadata.altitudeMeters, 'f', 2)));
        }
    }

    if (settings.includeCopyrightCreator) {
        addXmpElement(body, QStringLiteral("Creator"), metadata.creator);
        addXmpElement(body, QStringLiteral("Copyright"), metadata.copyrightNotice);
    }

    if (settings.includeDescriptionKeywords) {
        addXmpElement(body, QStringLiteral("Description"), metadata.description);
        addXmpElement(body, QStringLiteral("Keywords"), metadata.keywords);
    }

    if (settings.includeDeviceIdentifiers) {
        addXmpElement(body, QStringLiteral("CameraSerialNumber"), metadata.cameraSerial);
        addXmpElement(body, QStringLiteral("LensSerialNumber"), metadata.lensSerial);
    }

    if (body.isEmpty()) {
        return {};
    }

    return (QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\"?>")
            + QStringLiteral("<x:xmpmeta xmlns:x=\"adobe:ns:meta/\">")
            + QStringLiteral("<rdf:RDF xmlns:rdf=\"http://www.w3.org/1999/02/22-rdf-syntax-ns#\">")
            + QStringLiteral("<rdf:Description rdf:about=\"\" xmlns:apollo=\"https://apollo.photo/ns/metadata/1.0/\">")
            + body
            + QStringLiteral("</rdf:Description></rdf:RDF></x:xmpmeta>"))
        .toUtf8();
}

QByteArray imageWriterFormat(ExportDialog::Format format)
{
    switch (format) {
    case ExportDialog::Format::Png:
        return QByteArrayLiteral("png");
    case ExportDialog::Format::Tiff:
        return QByteArrayLiteral("tiff");
    case ExportDialog::Format::WebP:
        return QByteArrayLiteral("webp");
    case ExportDialog::Format::Jpeg:
        return QByteArrayLiteral("jpeg");
    case ExportDialog::Format::Avif:
    case ExportDialog::Format::Original:
    default:
        return {};
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

    if (settings.format == ExportDialog::Format::Avif) {
        return imaging::AvifEncoder::write(image,
                                           filePath,
                                           settings.quality,
                                           avifXmpForMetadata(metadata, settings),
                                           image.colorSpace().iccProfile(),
                                           error);
    }

    const QByteArray format = imageWriterFormat(settings.format);
    if (format.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The selected export format cannot be rendered.");
        }
        return false;
    }

    if (!QImageWriter::supportedImageFormats().contains(format)) {
        if (error) {
            *error = QStringLiteral("This Apollo build is missing support for the selected image format.");
        }
        return false;
    }

    QSaveFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }

    {
        QImageWriter writer(&file, format);
        if (settings.format == ExportDialog::Format::Jpeg
            || settings.format == ExportDialog::Format::WebP) {
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
                                  tr("Apollo could not create the export folder:\n%1")
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

    const QString extension = extensionForFormat(settings.format, sourcePath);
    const QString suggestedPath = QDir(exportDirectory).filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_export") + extension);

    QString exportPath = QFileDialog::getSaveFileName(
        m_window,
        tr("Export Photo"),
        suggestedPath,
        filterForFormat(settings.format, sourcePath));

    if (exportPath.isEmpty()) {
        return;
    }

    exportPath = exportPathWithFormat(exportPath, settings.format, sourcePath);

    if (settings.format == ExportDialog::Format::Original) {
        QString error;
        if (!copyOriginalFile(sourcePath, exportPath, &error)) {
            QMessageBox::critical(m_window,
                                  tr("Export Failed"),
                                  tr("Apollo could not copy the original photo:\n%1\n\n%2")
                                      .arg(QDir::toNativeSeparators(exportPath), error));
        }
        return;
    }

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

    QString error;
    const QImage resizedImage = resizeForExport(rendered.image, settings);
    const QImage outputImage = convertToOutputColorSpace(resizedImage, settings, &error);
    if (outputImage.isNull()) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Apollo could not prepare the selected output color space.\n\n%1")
                                  .arg(error));
        return;
    }

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
