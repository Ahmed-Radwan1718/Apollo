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

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(18, 18, 18, 18);
    rootLayout->setSpacing(14);

    auto *fileGroup = new QGroupBox(tr("File Settings"), this);
    auto *fileLayout = new QFormLayout(fileGroup);
    fileLayout->setContentsMargins(14, 16, 14, 14);
    fileLayout->setHorizontalSpacing(14);
    fileLayout->setVerticalSpacing(10);

    m_formatCombo = new QComboBox(fileGroup);
    m_formatCombo->addItem(tr("JPEG"));
    m_formatCombo->addItem(tr("PNG"));
    fileLayout->addRow(tr("Format"), m_formatCombo);

    m_qualityLabel = new QLabel(tr("Quality"), fileGroup);
    auto *qualityWidget = new QWidget(fileGroup);
    auto *qualityLayout = new QHBoxLayout(qualityWidget);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(8);

    m_qualitySlider = new QSlider(Qt::Horizontal, qualityWidget);
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setValue(95);
    m_qualitySpinBox = new QSpinBox(qualityWidget);
    m_qualitySpinBox->setRange(1, 100);
    m_qualitySpinBox->setValue(95);
    m_qualitySpinBox->setSuffix(QStringLiteral("%"));
    m_qualitySpinBox->setFixedWidth(72);

    qualityLayout->addWidget(m_qualitySlider, 1);
    qualityLayout->addWidget(m_qualitySpinBox);
    fileLayout->addRow(m_qualityLabel, qualityWidget);
    rootLayout->addWidget(fileGroup);

    auto *sizeGroup = new QGroupBox(tr("Image Sizing"), this);
    auto *sizeLayout = new QFormLayout(sizeGroup);
    sizeLayout->setContentsMargins(14, 16, 14, 14);
    sizeLayout->setHorizontalSpacing(14);
    sizeLayout->setVerticalSpacing(10);

    m_sizeCombo = new QComboBox(sizeGroup);
    m_sizeCombo->addItem(tr("Original Size"));
    m_sizeCombo->addItem(tr("50%"));
    m_sizeCombo->addItem(tr("25%"));
    m_sizeCombo->addItem(tr("Custom Long Edge"));
    sizeLayout->addRow(tr("Size"), m_sizeCombo);

    m_longEdgeLabel = new QLabel(tr("Long edge"), sizeGroup);
    m_longEdgeSpinBox = new QSpinBox(sizeGroup);
    m_longEdgeSpinBox->setRange(64, 30000);
    m_longEdgeSpinBox->setValue(2048);
    m_longEdgeSpinBox->setSuffix(tr(" px"));
    sizeLayout->addRow(m_longEdgeLabel, m_longEdgeSpinBox);
    rootLayout->addWidget(sizeGroup);

    auto *metadataGroup = new QGroupBox(tr("Metadata"), this);
    auto *metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->setContentsMargins(14, 14, 14, 14);
    m_includeMetadataCheckBox = new QCheckBox(
        tr("Include available photo metadata"), metadataGroup);
    m_includeMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_includeMetadataCheckBox);
    rootLayout->addWidget(metadataGroup);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                         Qt::Horizontal,
                                         this);
    if (QPushButton *okButton = buttons->button(QDialogButtonBox::Ok)) {
        okButton->setText(tr("Export…"));
        okButton->setDefault(true);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
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
        "QDialog { background: #181818; color: #d8d8d8; }"
        "QGroupBox { color: #cfcfcf; border: 1px solid #303030; margin-top: 8px;"
        "  font-weight: 600; padding-top: 4px; }"
        "QGroupBox::title { subcontrol-origin: margin; left: 10px; padding: 0 4px; }"
        "QLabel, QCheckBox { color: #c5c5c5; }"
        "QComboBox, QSpinBox { background: #202020; color: #dedede;"
        "  border: 1px solid #383838; padding: 4px 6px; min-height: 22px; }"
        "QComboBox QAbstractItemView { background: #202020; color: #dedede;"
        "  selection-background-color: #3a3a3a; }"
        "QPushButton { background: #292929; color: #dedede; border: 1px solid #3a3a3a;"
        "  padding: 6px 14px; min-width: 72px; }"
        "QPushButton:hover { background: #323232; }"
        "QPushButton:default { border-color: #6a6a6a; }"));

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
