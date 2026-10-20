#include "app/ExportDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

ExportDialog::ExportDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Export"));
    setModal(true);
    setMinimumWidth(430);
    setObjectName(QStringLiteral("exportDialog"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 12, 18, 16);
    rootLayout->setSpacing(4);

    auto *fileGroup = new QGroupBox(tr("File Settings"), this);
    fileGroup->setObjectName(QStringLiteral("exportSection"));
    auto *fileLayout = new QFormLayout(fileGroup);
    fileLayout->setContentsMargins(12, 14, 12, 12);
    fileLayout->setHorizontalSpacing(14);
    fileLayout->setVerticalSpacing(10);

    m_formatCombo = new QComboBox(fileGroup);
    m_formatCombo->setObjectName(QStringLiteral("exportCombo"));
    m_formatCombo->addItem(tr("JPEG"));
    m_formatCombo->addItem(tr("PNG"));
    fileLayout->addRow(tr("Format"), m_formatCombo);

    m_qualityLabel = new QLabel(tr("Quality"), fileGroup);
    auto *qualityWidget = new QWidget(fileGroup);
    auto *qualityLayout = new QHBoxLayout(qualityWidget);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(8);

    m_qualitySlider = new QSlider(Qt::Horizontal, qualityWidget);
    m_qualitySlider->setObjectName(QStringLiteral("exportQualitySlider"));
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setValue(95);
    m_qualitySpinBox = new QSpinBox(qualityWidget);
    m_qualitySpinBox->setObjectName(QStringLiteral("exportSpinBox"));
    m_qualitySpinBox->setRange(1, 100);
    m_qualitySpinBox->setValue(95);
    m_qualitySpinBox->setSuffix(QStringLiteral("%"));
    m_qualitySpinBox->setFixedWidth(72);

    qualityLayout->addWidget(m_qualitySlider, 1);
    qualityLayout->addWidget(m_qualitySpinBox);
    fileLayout->addRow(m_qualityLabel, qualityWidget);
    rootLayout->addWidget(fileGroup);

    auto *sizeGroup = new QGroupBox(tr("Image Sizing"), this);
    sizeGroup->setObjectName(QStringLiteral("exportSection"));
    auto *sizeLayout = new QFormLayout(sizeGroup);
    sizeLayout->setContentsMargins(12, 14, 12, 12);
    sizeLayout->setHorizontalSpacing(14);
    sizeLayout->setVerticalSpacing(10);

    m_sizeCombo = new QComboBox(sizeGroup);
    m_sizeCombo->setObjectName(QStringLiteral("exportCombo"));
    m_sizeCombo->addItem(tr("Original Size"));
    m_sizeCombo->addItem(tr("50%"));
    m_sizeCombo->addItem(tr("25%"));
    m_sizeCombo->addItem(tr("Custom Long Edge"));
    sizeLayout->addRow(tr("Size"), m_sizeCombo);

    m_longEdgeLabel = new QLabel(tr("Long edge"), sizeGroup);
    m_longEdgeSpinBox = new QSpinBox(sizeGroup);
    m_longEdgeSpinBox->setObjectName(QStringLiteral("exportSpinBox"));
    m_longEdgeSpinBox->setRange(64, 30000);
    m_longEdgeSpinBox->setValue(2048);
    m_longEdgeSpinBox->setSuffix(tr(" px"));
    sizeLayout->addRow(m_longEdgeLabel, m_longEdgeSpinBox);
    rootLayout->addWidget(sizeGroup);

    auto *metadataGroup = new QGroupBox(tr("Metadata"), this);
    metadataGroup->setObjectName(QStringLiteral("exportSection"));
    auto *metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->setContentsMargins(12, 14, 12, 12);
    m_includeMetadataCheckBox = new QCheckBox(
        tr("Include available photo metadata"), metadataGroup);
    m_includeMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_includeMetadataCheckBox);
    rootLayout->addWidget(metadataGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                         Qt::Horizontal,
                                         this);
    buttons->setObjectName(QStringLiteral("exportButtons"));
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok)) {
        okButton->setText(tr("Export…"));
        okButton->setDefault(true);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addSpacing(8);
    rootLayout->addWidget(buttons);

    connect(m_formatCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateControlStates(); });
    connect(m_sizeCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateControlStates(); });
    connect(m_qualitySlider, &QSlider::valueChanged,
            m_qualitySpinBox, &QSpinBox::setValue);
    connect(m_qualitySpinBox, &QSpinBox::valueChanged,
            m_qualitySlider, &QSlider::setValue);

    setStyleSheet(QStringLiteral(
        "QDialog#exportDialog {"
        "  background: #181818;"
        "  color: #d6d6d6;"
        "  font-family: \"Segoe UI Variable\";"
        "  font-size: 12px;"
        "}"
        "QGroupBox#exportSection {"
        "  background: transparent;"
        "  color: #d6d6d6;"
        "  border: 0;"
        "  border-top: 1px solid #2a2a2a;"
        "  margin-top: 18px;"
        "  padding-top: 0;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QGroupBox#exportSection::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 0;"
        "  top: -8px;"
        "  padding: 0 8px 0 0;"
        "  background: #181818;"
        "  color: #d6d6d6;"
        "}"
        "QLabel {"
        "  color: #d0d0d0;"
        "  font-size: 12px;"
        "}"
        "QLabel:disabled { color: #666666; }"
        "QCheckBox {"
        "  color: #d0d0d0;"
        "  spacing: 8px;"
        "  font-size: 12px;"
        "}"
        "QComboBox#exportCombo, QSpinBox#exportSpinBox {"
        "  background: #181818;"
        "  color: #cfcfcf;"
        "  border: 1px solid #303030;"
        "  border-radius: 0;"
        "  padding: 4px 7px;"
        "  min-height: 22px;"
        "  selection-background-color: #303030;"
        "}"
        "QComboBox#exportCombo:hover, QSpinBox#exportSpinBox:hover {"
        "  border-color: #4a4a4a;"
        "}"
        "QComboBox#exportCombo:focus, QSpinBox#exportSpinBox:focus {"
        "  border-color: #6a6a6a;"
        "}"
        "QComboBox#exportCombo:disabled, QSpinBox#exportSpinBox:disabled {"
        "  color: #666666;"
        "  border-color: #262626;"
        "}"
        "QComboBox#exportCombo QAbstractItemView {"
        "  background: #1b1b1b;"
        "  color: #d0d0d0;"
        "  border: 1px solid #303030;"
        "  selection-background-color: #303030;"
        "  selection-color: #ffffff;"
        "  outline: 0;"
        "}"
        "QSlider#exportQualitySlider {"
        "  min-height: 14px;"
        "  max-height: 14px;"
        "}"
        "QSlider#exportQualitySlider::groove:horizontal {"
        "  height: 2px;"
        "  border-radius: 1px;"
        "  background: qlineargradient(x1:0, y1:0, x2:1, y2:0,"
        "    stop:0 #303030, stop:0.5 #626262, stop:1 #9b9b9b);"
        "}"
        "QSlider#exportQualitySlider::sub-page:horizontal,"
        "QSlider#exportQualitySlider::add-page:horizontal {"
        "  background: transparent;"
        "}"
        "QSlider#exportQualitySlider::handle:horizontal {"
        "  width: 10px;"
        "  height: 10px;"
        "  margin: -4px 0;"
        "  border-radius: 5px;"
        "  background: #d9d9d9;"
        "  border: 0;"
        "}"
        "QSlider#exportQualitySlider::handle:horizontal:hover {"
        "  background: #ffffff;"
        "}"
        "QSlider#exportQualitySlider:disabled::groove:horizontal {"
        "  background: #303030;"
        "}"
        "QSlider#exportQualitySlider:disabled::handle:horizontal {"
        "  background: #666666;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton {"
        "  background: transparent;"
        "  color: #d0d0d0;"
        "  border: 1px solid #3a3a3a;"
        "  border-radius: 0;"
        "  padding: 6px 16px;"
        "  min-width: 74px;"
        "  font-size: 12px;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton:hover {"
        "  color: #ffffff;"
        "  background: #252525;"
        "  border-color: #5a5a5a;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton:pressed {"
        "  background: #303030;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton:default {"
        "  color: #ffffff;"
        "  border-color: #747474;"
        "}"));

    updateControlStates();
}

ExportDialog::Settings ExportDialog::settings() const
{
    Settings result;
    result.format = m_formatCombo->currentIndex() == 1 ? Format::Png : Format::Jpeg;

    switch (m_sizeCombo->currentIndex()) {
    case 1:
        result.resizeMode = ResizeMode::Percent50;
        break;
    case 2:
        result.resizeMode = ResizeMode::Percent25;
        break;
    case 3:
        result.resizeMode = ResizeMode::LongEdge;
        break;
    default:
        result.resizeMode = ResizeMode::Original;
        break;
    }

    result.longEdgePixels = m_longEdgeSpinBox->value();
    result.quality = m_qualitySpinBox->value();
    result.includeMetadata = m_includeMetadataCheckBox->isChecked();
    return result;
}

void ExportDialog::updateControlStates()
{
    const bool jpeg = m_formatCombo->currentIndex() == 0;
    m_qualityLabel->setEnabled(jpeg);
    m_qualitySlider->setEnabled(jpeg);
    m_qualitySpinBox->setEnabled(jpeg);

    const bool customLongEdge = m_sizeCombo->currentIndex() == 3;
    m_longEdgeLabel->setEnabled(customLongEdge);
    m_longEdgeSpinBox->setEnabled(customLongEdge);
}
