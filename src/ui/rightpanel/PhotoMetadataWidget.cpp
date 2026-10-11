#include "ui/rightpanel/PhotoMetadataWidget.h"

#include "ui/sidebar/SidebarCollapseHandle.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>

#include <cmath>

namespace {

struct MetadataReadout {
    QWidget *widget = nullptr;
    QLabel *value = nullptr;
};

MetadataReadout createReadout(const QString &caption, QWidget *parent)
{
    auto *container = new QWidget(parent);
    auto *layout = new QVBoxLayout(container);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(1);

    auto *value = new QLabel(QStringLiteral("—"), container);
    value->setObjectName(QStringLiteral("metadataValue"));
    value->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    auto *label = new QLabel(caption, container);
    label->setObjectName(QStringLiteral("metadataCaption"));
    label->setAlignment(Qt::AlignHCenter | Qt::AlignVCenter);

    layout->addWidget(value);
    layout->addWidget(label);

    return {container, value};
}

QString formatShutter(double seconds)
{
    if (seconds <= 0.0) {
        return QStringLiteral("—");
    }

    if (seconds < 1.0) {
        const double reciprocal = 1.0 / seconds;
        const auto denominator = static_cast<qlonglong>(std::llround(reciprocal));
        if (denominator > 0 && std::abs(reciprocal - static_cast<double>(denominator)) < 0.08) {
            return QStringLiteral("1/%1").arg(denominator);
        }
    }

    if (seconds >= 10.0) {
        return QStringLiteral("%1 s").arg(QString::number(seconds, 'f', 0));
    }

    return QStringLiteral("%1 s").arg(QString::number(seconds, 'g', 3));
}

QString formatAperture(double aperture)
{
    return aperture > 0.0
        ? QStringLiteral("f/%1").arg(QString::number(aperture, 'g', 3))
        : QStringLiteral("—");
}

QString formatIso(double iso)
{
    return iso > 0.0
        ? QString::number(static_cast<qlonglong>(std::llround(iso)))
        : QStringLiteral("—");
}

QString formatFocalLength(double millimeters)
{
    if (millimeters <= 0.0) {
        return QStringLiteral("—");
    }

    const double rounded = std::round(millimeters);
    if (std::abs(millimeters - rounded) < 0.05) {
        return QStringLiteral("%1 mm").arg(QString::number(static_cast<qlonglong>(rounded)));
    }

    return QStringLiteral("%1 mm").arg(QString::number(millimeters, 'g', 3));
}

} // namespace

PhotoMetadataWidget::PhotoMetadataWidget(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("photoMetadataWidget"));

    auto *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 4);
    layout->setSpacing(0);

    const MetadataReadout shutter = createReadout(tr("Shutter"), this);
    const MetadataReadout aperture = createReadout(tr("Aperture"), this);
    const MetadataReadout iso = createReadout(tr("ISO"), this);
    const MetadataReadout focal = createReadout(tr("Focal"), this);

    m_shutterValue = shutter.value;
    m_apertureValue = aperture.value;
    m_isoValue = iso.value;
    m_focalLengthValue = focal.value;

    layout->addWidget(shutter.widget, 1);
    layout->addWidget(aperture.widget, 1);
    layout->addWidget(iso.widget, 1);
    layout->addWidget(focal.widget, 1);

    setStyleSheet(QStringLiteral(
        "QWidget#photoMetadataWidget {"
        "  background: transparent;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#metadataValue {"
        "  color: #d1d1d1;"
        "  font-size: 12px;"
        "  font-weight: 500;"
        "}"
        "QLabel#metadataCaption {"
        "  color: #747474;"
        "  font-size: 10px;"
        "}"));

    if (QWidget *sidePanel = parentWidget()) {
        new SidebarCollapseHandle(sidePanel,
                                  SidebarCollapseHandle::Side::Right);
    }
}

void PhotoMetadataWidget::setMetadata(const imaging::PhotoMetadata &metadata)
{
    m_shutterValue->setText(formatShutter(metadata.shutterSeconds));
    m_apertureValue->setText(formatAperture(metadata.aperture));
    m_isoValue->setText(formatIso(metadata.iso));
    m_focalLengthValue->setText(formatFocalLength(metadata.focalLengthMm));
}

void PhotoMetadataWidget::clear()
{
    setMetadata({});
}
