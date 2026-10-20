#include "app/PhotoExportController.h"

#include "app/HistoryModel.h"
#include "imaging/ArwDecoder.h"
#include "imaging/ImageLoader.h"
#include "ui/filmstrip/FilmstripWidget.h"

#include <QAction>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QImageWriter>
#include <QKeySequence>
#include <QMessageBox>
#include <QSaveFile>
#include <QWidget>

namespace {

bool isArwPath(const QString &filePath)
{
    return QFileInfo(filePath).suffix().compare(QStringLiteral("arw"),
                                               Qt::CaseInsensitive) == 0;
}

QString exportPathWithExtension(QString filePath, const QString &selectedFilter)
{
    if (!QFileInfo(filePath).suffix().isEmpty()) {
        return filePath;
    }

    filePath += selectedFilter.startsWith(QStringLiteral("PNG"), Qt::CaseInsensitive)
        ? QStringLiteral(".png")
        : QStringLiteral(".jpg");
    return filePath;
}

QImage renderForExport(const QString &filePath, QString *error)
{
    if (!isArwPath(filePath)) {
        const imaging::ImageLoadResult result = imaging::ImageLoader::load(filePath);
        if (!result.succeeded()) {
            if (error) {
                *error = result.error;
            }
            return {};
        }
        return result.image;
    }

    HistoryModel history;
    const std::optional<DevelopState> savedState = history.currentState(filePath);
    if (!savedState) {
        return imaging::ArwDecoder::decode(filePath, error);
    }

    const DevelopState &state = *savedState;
    return imaging::ArwDecoder::decode(filePath,
                                       error,
                                       nullptr,
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
                                       false);
}

bool writeExportImage(const QImage &image, const QString &filePath, QString *error)
{
    if (image.isNull()) {
        if (error) {
            *error = QStringLiteral("The rendered image is empty.");
        }
        return false;
    }

    const QString suffix = QFileInfo(filePath).suffix().toLower();
    const QByteArray format = suffix == QStringLiteral("png")
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
        if (format == QByteArrayLiteral("jpeg")) {
            writer.setQuality(95);
        }

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
    const QString suggestedPath = QDir(sourceInfo.absolutePath()).filePath(
        sourceInfo.completeBaseName() + QStringLiteral("_export.jpg"));

    QString selectedFilter = tr("JPEG Image (*.jpg *.jpeg)");
    QString exportPath = QFileDialog::getSaveFileName(
        m_window,
        tr("Export Photo"),
        suggestedPath,
        tr("JPEG Image (*.jpg *.jpeg);;PNG Image (*.png)"),
        &selectedFilter);

    if (exportPath.isEmpty()) {
        return;
    }

    exportPath = exportPathWithExtension(exportPath, selectedFilter);

    QString error;
    const QImage image = renderForExport(sourcePath, &error);
    if (image.isNull()) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Apollo could not render the photo for export.\n\n%1")
                                  .arg(error));
        return;
    }

    if (!writeExportImage(image, exportPath, &error)) {
        QMessageBox::critical(m_window,
                              tr("Export Failed"),
                              tr("Apollo could not save:\n%1\n\n%2")
                                  .arg(QDir::toNativeSeparators(exportPath), error));
    }
}
