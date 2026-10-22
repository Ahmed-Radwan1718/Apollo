#include "app/ExportDialog.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>
#include <QWidget>

ExportDialog::ExportDialog(const QString &sourceDirectory, QWidget *parent)
    : QDialog(parent)
    , m_sourceDirectory(sourceDirectory)
{
    setWindowTitle(tr("Export"));
    setModal(true);
    resize(460, 500);
    setMinimumWidth(460);
    setObjectName(QStringLiteral("exportDialog"));

    auto *rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(20, 12, 20, 16);
    rootLayout->setSpacing(6);

    auto *fileGroup = new QGroupBox(tr("File"), this);
    fileGroup->setObjectName(QStringLiteral("exportSection"));
    auto *fileLayout = new QFormLayout(fileGroup);
    fileLayout->setContentsMargins(12, 18, 12, 10);
    fileLayout->setHorizontalSpacing(14);
    fileLayout->setVerticalSpacing(10);
    fileLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    fileLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_formatCombo = new QComboBox(fileGroup);
    m_formatCombo->setObjectName(QStringLiteral("exportCombo"));
    m_formatCombo->addItem(tr("JPEG"));
    m_formatCombo->addItem(tr("PNG"));
    fileLayout->addRow(tr("Format"), m_formatCombo);

    m_qualityLabel = new QLabel(tr("Quality"), fileGroup);
    auto *qualityRow = new QWidget(fileGroup);
    auto *qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(10);

    m_qualitySlider = new QSlider(Qt::Horizontal, qualityRow);
    m_qualitySlider->setObjectName(QStringLiteral("exportQualitySlider"));
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setValue(95);

    m_qualitySpinBox = new QSpinBox(qualityRow);
    m_qualitySpinBox->setObjectName(QStringLiteral("exportSpinBox"));
    m_qualitySpinBox->setRange(1, 100);
    m_qualitySpinBox->setValue(95);
    m_qualitySpinBox->setSuffix(QStringLiteral("%"));
    m_qualitySpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_qualitySpinBox->setFixedWidth(58);

    qualityLayout->addWidget(m_qualitySlider, 1);
    qualityLayout->addWidget(m_qualitySpinBox);
    fileLayout->addRow(m_qualityLabel, qualityRow);

    m_locationCombo = new QComboBox(fileGroup);
    m_locationCombo->setObjectName(QStringLiteral("exportCombo"));
    m_locationCombo->addItem(tr("Same as original"));
    m_locationCombo->addItem(tr("Subfolder of original"));
    m_locationCombo->addItem(tr("Choose folder"));
    fileLayout->addRow(tr("Location"), m_locationCombo);

    m_subfolderLabel = new QLabel(tr("Subfolder"), fileGroup);
    m_subfolderEdit = new QLineEdit(fileGroup);
    m_subfolderEdit->setObjectName(QStringLiteral("exportLineEdit"));
    m_subfolderEdit->setText(tr("Exports"));
    fileLayout->addRow(m_subfolderLabel, m_subfolderEdit);

    m_folderLabel = new QLabel(tr("Folder"), fileGroup);
    m_folderRow = new QWidget(fileGroup);
    auto *folderLayout = new QHBoxLayout(m_folderRow);
    folderLayout->setContentsMargins(0, 0, 0, 0);
    folderLayout->setSpacing(8);

    m_folderEdit = new QLineEdit(m_folderRow);
    m_folderEdit->setObjectName(QStringLiteral("exportLineEdit"));
    m_folderEdit->setReadOnly(true);
    m_folderEdit->setPlaceholderText(tr("Choose a folder"));

    m_browseButton = new QPushButton(tr("…"), m_folderRow);
    m_browseButton->setObjectName(QStringLiteral("exportBrowseButton"));
    m_browseButton->setFixedWidth(34);

    folderLayout->addWidget(m_folderEdit, 1);
    folderLayout->addWidget(m_browseButton);
    fileLayout->addRow(m_folderLabel, m_folderRow);
    rootLayout->addWidget(fileGroup);

    auto *sizeGroup = new QGroupBox(tr("Image Size"), this);
    sizeGroup->setObjectName(QStringLiteral("exportSection"));
    auto *sizeLayout = new QFormLayout(sizeGroup);
    sizeLayout->setContentsMargins(12, 18, 12, 10);
    sizeLayout->setHorizontalSpacing(14);
    sizeLayout->setVerticalSpacing(10);
    sizeLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    sizeLayout->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);

    m_sizeCombo = new QComboBox(sizeGroup);
    m_sizeCombo->setObjectName(QStringLiteral("exportCombo"));
    m_sizeCombo->addItem(tr("Original Size"));
    m_sizeCombo->addItem(tr("Long Edge"));
    sizeLayout->addRow(tr("Resize"), m_sizeCombo);

    m_longEdgeLabel = new QLabel(tr("Long edge"), sizeGroup);
    m_longEdgeSpinBox = new QSpinBox(sizeGroup);
    m_longEdgeSpinBox->setObjectName(QStringLiteral("exportSpinBox"));
    m_longEdgeSpinBox->setRange(64, 30000);
    m_longEdgeSpinBox->setValue(2048);
    m_longEdgeSpinBox->setSuffix(tr(" px"));
    m_longEdgeSpinBox->setButtonSymbols(QAbstractSpinBox::NoButtons);
    sizeLayout->addRow(m_longEdgeLabel, m_longEdgeSpinBox);
    rootLayout->addWidget(sizeGroup);

    auto *metadataGroup = new QGroupBox(tr("Metadata"), this);
    metadataGroup->setObjectName(QStringLiteral("exportSection"));
    auto *metadataLayout = new QVBoxLayout(metadataGroup);
    metadataLayout->setContentsMargins(12, 18, 12, 10);

    m_includeMetadataCheckBox = new QCheckBox(
        tr("Include available photo metadata"), metadataGroup);
    m_includeMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_includeMetadataCheckBox);
    rootLayout->addWidget(metadataGroup);

    rootLayout->addStretch(1);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Ok,
                                         Qt::Horizontal,
                                         this);
    buttons->setObjectName(QStringLiteral("exportButtons"));
    m_exportButton = buttons->button(QDialogButtonBox::Ok);
    if (m_exportButton) {
        m_exportButton->setText(tr("Export"));
        m_exportButton->setDefault(true);
    }
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    rootLayout->addWidget(buttons);

    connect(m_formatCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateControlStates(); });
    connect(m_locationCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateControlStates(); });
    connect(m_sizeCombo, &QComboBox::currentIndexChanged,
            this, [this](int) { updateControlStates(); });
    connect(m_qualitySlider, &QSlider::valueChanged,
            m_qualitySpinBox, &QSpinBox::setValue);
    connect(m_qualitySpinBox, &QSpinBox::valueChanged,
            m_qualitySlider, &QSlider::setValue);
    connect(m_subfolderEdit, &QLineEdit::textChanged,
            this, [this](const QString &) { updateControlStates(); });
    connect(m_browseButton, &QPushButton::clicked,
            this, &ExportDialog::chooseCustomFolder);

    setStyleSheet(QStringLiteral(
        "QDialog#exportDialog {"
        "  background: #181818;"
        "  color: #d6d6d6;"
        "  font-family: \"Segoe UI Variable\";"
        "  font-size: 12px;"
        "}"
        "QGroupBox#exportSection {"
        "  background: transparent;"
        "  color: #dddddd;"
        "  border: 0;"
        "  border-top: 1px solid #2d2d2d;"
        "  margin-top: 17px;"
        "  padding-top: 0;"
        "  font-size: 13px;"
        "  font-weight: 600;"
        "}"
        "QGroupBox#exportSection::title {"
        "  subcontrol-origin: margin;"
        "  subcontrol-position: top left;"
        "  left: 0;"
        "  padding: 0 8px 0 0;"
        "  background: #181818;"
        "}"
        "QLabel { color: #cfcfcf; }"
        "QLabel:disabled { color: #676767; }"
        "QCheckBox { color: #cfcfcf; spacing: 8px; }"
        "QComboBox#exportCombo, QSpinBox#exportSpinBox, QLineEdit#exportLineEdit {"
        "  background: #1a1a1a;"
        "  color: #d5d5d5;"
        "  border: 1px solid #343434;"
        "  border-radius: 2px;"
        "  padding: 5px 8px;"
        "  min-height: 22px;"
        "  selection-background-color: #3a3a3a;"
        "}"
        "QComboBox#exportCombo:hover, QSpinBox#exportSpinBox:hover,"
        "QLineEdit#exportLineEdit:hover { border-color: #4a4a4a; }"
        "QComboBox#exportCombo:focus, QSpinBox#exportSpinBox:focus,"
        "QLineEdit#exportLineEdit:focus { border-color: #666666; }"
        "QComboBox#exportCombo:disabled, QSpinBox#exportSpinBox:disabled,"
        "QLineEdit#exportLineEdit:disabled {"
        "  color: #666666;"
        "  border-color: #292929;"
        "}"
        "QLineEdit#exportLineEdit:read-only { color: #bdbdbd; }"
        "QComboBox#exportCombo::drop-down { border: 0; width: 24px; }"
        "QComboBox#exportCombo QAbstractItemView {"
        "  background: #1b1b1b;"
        "  color: #d0d0d0;"
        "  border: 1px solid #343434;"
        "  selection-background-color: #343434;"
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
        "QSlider#exportQualitySlider::add-page:horizontal { background: transparent; }"
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
        "QSlider#exportQualitySlider:disabled::sub-page:horizontal,"
        "QSlider#exportQualitySlider:disabled::add-page:horizontal {"
        "  background: transparent;"
        "}"
        "QSlider#exportQualitySlider:disabled::handle:horizontal {"
        "  background: #666666;"
        "}"
        "QPushButton#exportBrowseButton {"
        "  background: #1a1a1a;"
        "  color: #d5d5d5;"
        "  border: 1px solid #343434;"
        "  border-radius: 2px;"
        "  min-height: 32px;"
        "  padding: 0;"
        "}"
        "QPushButton#exportBrowseButton:hover {"
        "  background: #252525;"
        "  border-color: #4a4a4a;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton {"
        "  background: transparent;"
        "  color: #d0d0d0;"
        "  border: 1px solid #3d3d3d;"
        "  border-radius: 2px;"
        "  padding: 6px 18px;"
        "  min-width: 82px;"
        "  min-height: 18px;"
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
        "  border-color: #777777;"
        "}"
        "QDialogButtonBox#exportButtons QPushButton:disabled {"
        "  color: #666666;"
        "  border-color: #303030;"
        "}"));

    updateControlStates();
}

ExportDialog::Settings ExportDialog::settings() const
{
    Settings result;
    result.format = m_formatCombo->currentIndex() == 1 ? Format::Png : Format::Jpeg;
    result.resizeMode = m_sizeCombo->currentIndex() == 1
        ? ResizeMode::LongEdge
        : ResizeMode::Original;

    switch (m_locationCombo->currentIndex()) {
    case 1:
        result.locationMode = LocationMode::OriginalSubfolder;
        break;
    case 2:
        result.locationMode = LocationMode::CustomFolder;
        break;
    default:
        result.locationMode = LocationMode::SameAsOriginal;
        break;
    }

    result.longEdgePixels = m_longEdgeSpinBox->value();
    result.quality = m_qualitySpinBox->value();
    result.includeMetadata = m_includeMetadataCheckBox->isChecked();
    result.subfolderName = m_subfolderEdit->text().trimmed();
    result.customFolder = m_customFolder;
    return result;
}

void ExportDialog::updateControlStates()
{
    const bool jpeg = m_formatCombo->currentIndex() == 0;
    m_qualityLabel->setEnabled(jpeg);
    m_qualitySlider->setEnabled(jpeg);
    m_qualitySpinBox->setEnabled(jpeg);

    const bool longEdge = m_sizeCombo->currentIndex() == 1;
    m_longEdgeLabel->setEnabled(longEdge);
    m_longEdgeSpinBox->setEnabled(longEdge);

    const bool subfolder = m_locationCombo->currentIndex() == 1;
    const bool customFolder = m_locationCombo->currentIndex() == 2;
    m_subfolderLabel->setVisible(subfolder);
    m_subfolderEdit->setVisible(subfolder);
    m_folderLabel->setVisible(customFolder);
    m_folderRow->setVisible(customFolder);

    bool canExport = true;
    if (subfolder) {
        canExport = !m_subfolderEdit->text().trimmed().isEmpty();
    } else if (customFolder) {
        canExport = !m_customFolder.isEmpty();
    }

    if (m_exportButton) {
        m_exportButton->setEnabled(canExport);
    }
}

void ExportDialog::chooseCustomFolder()
{
    const QString initialDirectory = m_customFolder.isEmpty()
        ? m_sourceDirectory
        : m_customFolder;
    const QString selectedDirectory = QFileDialog::getExistingDirectory(
        this,
        tr("Choose Export Folder"),
        initialDirectory,
        QFileDialog::ShowDirsOnly);

    if (selectedDirectory.isEmpty()) {
        return;
    }

    m_customFolder = selectedDirectory;
    m_folderEdit->setText(QDir::toNativeSeparators(selectedDirectory));
    updateControlStates();
}
