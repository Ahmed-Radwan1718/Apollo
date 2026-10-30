#pragma once

#include <QString>

namespace imaging {

struct PhotoMetadata {
    double shutterSeconds = 0.0;
    double aperture = 0.0;
    double iso = 0.0;
    double focalLengthMm = 0.0;
    int whiteBalanceTemperatureKelvin = 0;

    QString cameraMake;
    QString cameraModel;
    QString lensMake;
    QString lensModel;

    qint64 captureTimestamp = 0;

    bool hasLocation = false;
    double latitude = 0.0;
    double longitude = 0.0;
    bool hasAltitude = false;
    double altitudeMeters = 0.0;

    QString creator;
    QString copyrightNotice;
    QString description;
    QString keywords;

    QString cameraSerial;
    QString lensSerial;
};

} // namespace imaging
