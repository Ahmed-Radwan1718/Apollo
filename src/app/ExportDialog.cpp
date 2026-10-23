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
#include <QPainter>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>

namespace {

class ExportQualitySlider final : public QSlider
{
public:
    explicit ExportQualitySlider(QWidget *parent = nullptr)
        : QSlider(Qt::Horizontal, parent)
    {
        setFixedHeight(14);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        constexpr qreal handleRadius = 5.0;
        constexpr qreal grooveHeight = 2.0;
        const qreal left = handleRadius;
        const qreal right = qMax(left, width() - handleRadius);
        const qreal centerY = height() / 2.0;

        if (isEnabled()) {
            QLinearGradient gradient(left, centerY, right, centerY);
            gradient.setColorAt(0.0, QColor(QStringLiteral("#303030")));
            gradient.setColorAt(0.5, QColor(QStringLiteral("#626262")));
            gradient.setColorAt(1.0, QColor(QStringLiteral("#9b9b9b")));
            painter.setPen(Qt::NoPen);
            painter.setBrush(gradient);
        } else {
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(QStringLiteral("#303030")));
        }

        painter.drawRoundedRect(QRectF(left,
                                       centerY - grooveHeight / 2.0,
                                       qMax<qreal>(0.0, right - left),
                                       grooveHeight),
                                1.0,
                                1.0);

        const int available = qMax(0, static_cast<int>(right - left));
        const int handleOffset = QStyle::sliderPositionFromValue(
            minimum(), maximum(), value(), available, invertedAppearance());
        const qreal handleX = left + handleOffset;

        painter.setBrush(isEnabled()
                             ? (underMouse() ? QColor(QStringLiteral("#ffffff"))
                                             : QColor(QStringLiteral("#d9d9d9")))
                             : QColor(QStringLiteral("#666666")));
        painter.drawEllipse(QPointF(handleX, centerY), handleRadius, handleRadius);
    }
};

} // namespace

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
    fileGroup->setProperty("exportFileSection", true);
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

    m_locationCombo = new QComboBox(fileGroup);
    m_locationCombo->setObjectName(QStringLiteral("exportCombo"));
    m_locationCombo->addItem(tr("Same as original"));
    m_locationCombo->addItem(tr("Subfolder of original"));
    m_locationCombo->addItem(tr("Choose folder"));
    m_locationCombo->setEditable(true);
    m_locationCombo->lineEdit()->setReadOnly(true);
    m_locationCombo->lineEdit()->setAttribute(Qt::WA_TransparentForMouseEvents);
    m_locationCombo->lineEdit()->setCursorPosition(0);

    auto *formatLocationRow = new QWidget(fileGroup);
    auto *formatLocationLayout = new QHBoxLayout(formatLocationRow);
    formatLocationLayout->setContentsMargins(0, 0, 0, 0);
    formatLocationLayout->setSpacing(10);

    auto *formatLabel = new QLabel(tr("Format"), formatLocationRow);
    auto *locationLabel = new QLabel(tr("Location"), formatLocationRow);
    formatLocationLayout->addWidget(formatLabel);
    formatLocationLayout->addWidget(m_formatCombo, 1);
    formatLocationLayout->addSpacing(8);
    formatLocationLayout->addWidget(locationLabel);
    formatLocationLayout->addWidget(m_locationCombo, 1);
    fileLayout->addRow(formatLocationRow);

    m_qualityLabel = new QLabel(tr("Quality"), fileGroup);
    auto *qualityRow = new QWidget(fileGroup);
    auto *qualityLayout = new QHBoxLayout(qualityRow);
    qualityLayout->setContentsMargins(0, 0, 0, 0);
    qualityLayout->setSpacing(8);

    m_qualitySlider = new ExportQualitySlider(qualityRow);
    m_qualitySlider->setObjectName(QStringLiteral("exportQualitySlider"));
    m_qualitySlider->setRange(1, 100);
    m_qualitySlider->setValue(95);

    auto *qualityValueLabel = new QLabel(tr("95%"), qualityRow);
    qualityValueLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    qualityValueLabel->setFixedWidth(38);
    qualityValueLabel->setStyleSheet(QStringLiteral(
        "QLabel {"
        "  color: #9f9f9f;"
        "  background: transparent;"
        "  border: 0;"
        "  font-size: 11px;"
        "}"
        "QLabel:disabled { color: #676767; }"));

    qualityLayout->addWidget(m_qualitySlider, 1);
    qualityLayout->addWidget(qualityValueLabel);
    fileLayout->addRow(m_qualityLabel, qualityRow);

    m_subfolderLabel = new QLabel(tr("Subfolder"), fileGroup);
    m_subfolderEdit = new QLineEdit(fileGroup);
    m_subfolderEdit->setObjectName(QStringLiteral("exportLineEdit"));
    m_subfolderEdit->setText(tr("Exports"));
    fileLayout->addRow(m_subfolderLabel, m_subfolderEdit);

    rootLayout->addWidget(fileGroup);

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
            this, [this, qualityValueLabel](int) {
                updateControlStates();
                qualityValueLabel->setEnabled(m_formatCombo->currentIndex() == 0);
            });
    connect(m_locationCombo, &QComboBox::currentIndexChanged,
            this, [this](int index) {
                updateControlStates();
                if (index == 2) {
                    QTimer::singleShot(0, this, [this]() {
                        if (m_locationCombo->currentIndex() == 2) {
                            chooseCustomFolder();
                        }
                    });
                }
            });
    connect(m_qualitySlider, &QSlider::valueChanged,
            qualityValueLabel,
            [qualityValueLabel](int value) {
                qualityValueLabel->setText(QStringLiteral("%1%").arg(value));
            });
    connect(m_subfolderEdit, &QLineEdit::textChanged,
            this, [this](const QString &) { updateControlStates(); });

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
        "QGroupBox#exportSection[exportFileSection=\"true\"] {"
        "  margin-top: 22px;"
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

    result.quality = m_qualitySlider->value();
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

    const bool subfolder = m_locationCombo->currentIndex() == 1;
    const bool customFolder = m_locationCombo->currentIndex() == 2;
    m_subfolderLabel->setVisible(subfolder);
    m_subfolderEdit->setVisible(subfolder);

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
    const QString nativeDirectory = QDir::toNativeSeparators(selectedDirectory);
    m_locationCombo->setEditText(nativeDirectory);
    m_locationCombo->lineEdit()->setCursorPosition(0);
    updateControlStates();
}
