#include "imaging/AvifEncoder.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QProcess>
#include <QSaveFile>
#include <QTemporaryDir>

#include <algorithm>

namespace imaging {
namespace {

void setError(QString *error, const QString &message)
{
    if (error) {
        *error = message;
    }
}

QString bundledEncoderPath()
{
    return QDir(QCoreApplication::applicationDirPath())
        .filePath(QStringLiteral("avifenc.exe"));
}

QString processFailureMessage(QProcess &process)
{
    QString details = QString::fromUtf8(process.readAllStandardError()).trimmed();
    if (details.isEmpty()) {
        details = QString::fromUtf8(process.readAllStandardOutput()).trimmed();
    }
    if (details.isEmpty()) {
        details = process.errorString();
    }
    return details;
}

bool writeTemporaryPayload(const QString &filePath,
                           const QByteArray &payload,
                           const QString &description,
                           QString *error)
{
    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly)
        || file.write(payload) != payload.size()) {
        setError(error,
                 QStringLiteral("Apollo could not prepare %1 for AVIF export: %2")
                     .arg(description, file.errorString()));
        return false;
    }
    return true;
}

bool copyFileAtomically(const QString &sourcePath,
                        const QString &destinationPath,
                        QString *error)
{
    QFile source(sourcePath);
    if (!source.open(QIODevice::ReadOnly)) {
        setError(error, source.errorString());
        return false;
    }

    QSaveFile destination(destinationPath);
    if (!destination.open(QIODevice::WriteOnly)) {
        setError(error, destination.errorString());
        return false;
    }

    char buffer[1024 * 1024];
    while (true) {
        const qint64 bytesRead = source.read(buffer, sizeof(buffer));
        if (bytesRead < 0) {
            setError(error, source.errorString());
            destination.cancelWriting();
            return false;
        }
        if (bytesRead == 0) {
            break;
        }
        if (destination.write(buffer, bytesRead) != bytesRead) {
            setError(error, destination.errorString());
            destination.cancelWriting();
            return false;
        }
    }

    if (!destination.commit()) {
        setError(error, destination.errorString());
        return false;
    }

    return true;
}

} // namespace

bool AvifEncoder::write(const QImage &image,
                        const QString &filePath,
                        int quality,
                        const QByteArray &xmpMetadata,
                        const QByteArray &iccProfile,
                        QString *error)
{
    if (image.isNull()) {
        setError(error, QStringLiteral("The rendered image is empty."));
        return false;
    }

    const QString encoderPath = bundledEncoderPath();
    if (!QFile::exists(encoderPath)) {
        setError(error,
                 QStringLiteral("Apollo's bundled AVIF encoder is missing: %1")
                     .arg(QDir::toNativeSeparators(encoderPath)));
        return false;
    }

    QTemporaryDir temporaryDirectory;
    if (!temporaryDirectory.isValid()) {
        setError(error, QStringLiteral("Apollo could not create temporary files for AVIF export."));
        return false;
    }

    const QString inputPath = QDir(temporaryDirectory.path())
                                  .filePath(QStringLiteral("input.png"));
    QImageWriter pngWriter(inputPath, QByteArrayLiteral("png"));
    if (!pngWriter.write(image)) {
        setError(error,
                 QStringLiteral("Apollo could not prepare the AVIF input image: %1")
                     .arg(pngWriter.errorString()));
        return false;
    }

    QStringList arguments;
    arguments << QStringLiteral("--qcolor")
              << QString::number(std::clamp(quality, 1, 100))
              << QStringLiteral("--qalpha")
              << QStringLiteral("100")
              << QStringLiteral("--speed")
              << QStringLiteral("6");

    if (!xmpMetadata.isEmpty()) {
        const QString xmpPath = QDir(temporaryDirectory.path())
                                    .filePath(QStringLiteral("metadata.xmp"));
        if (!writeTemporaryPayload(xmpPath,
                                   xmpMetadata,
                                   QStringLiteral("metadata"),
                                   error)) {
            return false;
        }
        arguments << QStringLiteral("--xmp") << xmpPath;
    }

    if (!iccProfile.isEmpty()) {
        const QString iccPath = QDir(temporaryDirectory.path())
                                    .filePath(QStringLiteral("color-profile.icc"));
        if (!writeTemporaryPayload(iccPath,
                                   iccProfile,
                                   QStringLiteral("color profile"),
                                   error)) {
            return false;
        }
        arguments << QStringLiteral("--icc") << iccPath;
    }

    const QString encodedPath = QDir(temporaryDirectory.path())
                                    .filePath(QStringLiteral("output.avif"));
    arguments << inputPath << encodedPath;

    QProcess encoderProcess;
    encoderProcess.setProgram(encoderPath);
    encoderProcess.setArguments(arguments);
    encoderProcess.start();

    if (!encoderProcess.waitForStarted()) {
        setError(error,
                 QStringLiteral("Apollo could not start its bundled AVIF encoder: %1")
                     .arg(encoderProcess.errorString()));
        return false;
    }

    encoderProcess.waitForFinished(-1);
    if (encoderProcess.exitStatus() != QProcess::NormalExit
        || encoderProcess.exitCode() != 0) {
        setError(error,
                 QStringLiteral("AVIF encoding failed: %1")
                     .arg(processFailureMessage(encoderProcess)));
        return false;
    }

    if (!QFile::exists(encodedPath)) {
        setError(error, QStringLiteral("AVIF encoding finished without producing an output file."));
        return false;
    }

    return copyFileAtomically(encodedPath, filePath, error);
}

} // namespace imaging
