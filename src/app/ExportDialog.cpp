#include "app/ExportDialog.h"

#include <QAbstractSpinBox>
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QSettings>
#include <QSlider>
#include <QSpinBox>
#include <QStyle>
#include <QStyleOptionButton>
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

class ExportCheckBox final : public QCheckBox
{
public:
    explicit ExportCheckBox(const QString &text, QWidget *parent = nullptr)
        : QCheckBox(text, parent)
    {
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        QCheckBox::paintEvent(event);

        QStyleOptionButton option;
        initStyleOption(&option);
        const QRect indicatorRect = style()->subElementRect(
            QStyle::SE_CheckBoxIndicator, &option, this);
        if (!indicatorRect.isValid()) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);

        if (hasFocus()) {
            painter.setBrush(Qt::NoBrush);
            painter.setPen(QPen(QColor(QStringLiteral("#9b9b9b")), 1.0));
            painter.drawRoundedRect(QRectF(indicatorRect).adjusted(1.0, 1.0, -1.0, -1.0),
                                    2.5,
                                    2.5);
        }

        if (!isChecked()) {
            return;
        }

        QPen checkPen(isEnabled()
                          ? QColor(QStringLiteral("#181818"))
                          : QColor(QStringLiteral("#505050")),
                      1.8,
                      Qt::SolidLine,
                      Qt::RoundCap,
                      Qt::RoundJoin);
        painter.setPen(checkPen);
        painter.setBrush(Qt::NoBrush);

        const QRectF box(indicatorRect);
        QPainterPath checkPath;
        checkPath.moveTo(box.left() + box.width() * 0.24,
                         box.top() + box.height() * 0.53);
        checkPath.lineTo(box.left() + box.width() * 0.43,
                         box.top() + box.height() * 0.72);
        checkPath.lineTo(box.left() + box.width() * 0.78,
                         box.top() + box.height() * 0.31);
        painter.drawPath(checkPath);
    }
};

class ExportLocationComboBox final : public QComboBox
{
public:
    explicit ExportLocationComboBox(QWidget *parent = nullptr)
        : QComboBox(parent)
    {
        setEditable(true);
        lineEdit()->setReadOnly(true);
        lineEdit()->installEventFilter(this);
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == lineEdit() && event->type() == QEvent::MouseButtonPress) {
            const auto *mouseEvent = static_cast<QMouseEvent *>(event);
            if (mouseEvent->button() == Qt::LeftButton) {
                showPopup();
                return true;
            }
        }

        return QComboBox::eventFilter(watched, event);
    }

    void mousePressEvent(QMouseEvent *event) override
    {
        if (event->button() == Qt::LeftButton) {
            showPopup();
            event->accept();
            return;
        }

        QComboBox::mousePressEvent(event);
    }
};

ExportDialog::Settings loadRememberedExportSettings()
{
    ExportDialog::Settings remembered;
    QSettings settings;
    settings.beginGroup(QStringLiteral("exportDialog"));

    remembered.format = settings.value(QStringLiteral("format"), 0).toInt() == 1
        ? ExportDialog::Format::Png
        : ExportDialog::Format::Jpeg;

    switch (settings.value(QStringLiteral("locationMode"), 0).toInt()) {
    case 1:
        remembered.locationMode = ExportDialog::LocationMode::OriginalSubfolder;
        break;
    case 2:
        remembered.locationMode = ExportDialog::LocationMode::CustomFolder;
        break;
    default:
        remembered.locationMode = ExportDialog::LocationMode::SameAsOriginal;
        break;
    }

    remembered.quality = qBound(
        1,
        settings.value(QStringLiteral("quality"), remembered.quality).toInt(),
        100);

    const bool legacyIncludeMetadata = settings.value(
        QStringLiteral("includeMetadata"), true).toBool();
    remembered.includeCameraLensInfo = settings.value(
        QStringLiteral("metadataCameraLens"), legacyIncludeMetadata).toBool();
    remembered.includeCaptureSettings = settings.value(
        QStringLiteral("metadataCaptureSettings"), legacyIncludeMetadata).toBool();
    remembered.includeCaptureDateTime = settings.value(
        QStringLiteral("metadataCaptureDateTime"), legacyIncludeMetadata).toBool();
    remembered.includeLocationInfo = settings.value(
        QStringLiteral("metadataLocation"), false).toBool();
    remembered.includeCopyrightCreator = settings.value(
        QStringLiteral("metadataCopyrightCreator"), legacyIncludeMetadata).toBool();
    remembered.includeDescriptionKeywords = settings.value(
        QStringLiteral("metadataDescriptionKeywords"), legacyIncludeMetadata).toBool();
    remembered.includeDeviceIdentifiers = settings.value(
        QStringLiteral("metadataDeviceIdentifiers"), false).toBool();

    remembered.subfolderName = settings.value(
        QStringLiteral("subfolderName"), QStringLiteral("Exports")).toString();
    remembered.customFolder = settings.value(QStringLiteral("customFolder")).toString();

    settings.endGroup();
    return remembered;
}

void saveRememberedExportSettings(const ExportDialog::Settings &remembered)
{
    QSettings settings;
    settings.beginGroup(QStringLiteral("exportDialog"));

    settings.setValue(QStringLiteral("format"),
                      remembered.format == ExportDialog::Format::Png ? 1 : 0);

    int locationMode = 0;
    switch (remembered.locationMode) {
    case ExportDialog::LocationMode::OriginalSubfolder:
        locationMode = 1;
        break;
    case ExportDialog::LocationMode::CustomFolder:
        locationMode = 2;
        break;
    case ExportDialog::LocationMode::SameAsOriginal:
        break;
    }
    settings.setValue(QStringLiteral("locationMode"), locationMode);

    settings.setValue(QStringLiteral("quality"), remembered.quality);
    settings.setValue(QStringLiteral("metadataCameraLens"),
                      remembered.includeCameraLensInfo);
    settings.setValue(QStringLiteral("metadataCaptureSettings"),
                      remembered.includeCaptureSettings);
    settings.setValue(QStringLiteral("metadataCaptureDateTime"),
                      remembered.includeCaptureDateTime);
    settings.setValue(QStringLiteral("metadataLocation"),
                      remembered.includeLocationInfo);
    settings.setValue(QStringLiteral("metadataCopyrightCreator"),
                      remembered.includeCopyrightCreator);
    settings.setValue(QStringLiteral("metadataDescriptionKeywords"),
                      remembered.includeDescriptionKeywords);
    settings.setValue(QStringLiteral("metadataDeviceIdentifiers"),
                      remembered.includeDeviceIdentifiers);
    settings.setValue(QStringLiteral("subfolderName"), remembered.subfolderName);
    settings.setValue(QStringLiteral("customFolder"), remembered.customFolder);
    settings.endGroup();
}

} // namespace

ExportDialog::ExportDialog(const QString &sourceDirectory, QWidget *parent)
    : QDialog(parent)
    , m_sourceDirectory(sourceDirectory)
{
    setWindowTitle(tr("Export"));
    setModal(true);
    resize(460, 560);
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

    m_locationCombo = new ExportLocationComboBox(fileGroup);
    m_locationCombo->setObjectName(QStringLiteral("exportCombo"));
    m_locationCombo->addItem(tr("Same as original"));
    m_locationCombo->addItem(tr("Subfolder of original"));
    m_locationCombo->addItem(tr("Choose folder"));
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
    auto *metadataLayout = new QGridLayout(metadataGroup);
    metadataLayout->setContentsMargins(12, 20, 12, 10);
    metadataLayout->setHorizontalSpacing(28);
    metadataLayout->setVerticalSpacing(6);
    metadataLayout->setColumnStretch(0, 1);
    metadataLayout->setColumnStretch(1, 1);

    auto *technicalLabel = new QLabel(tr("Technical"), metadataGroup);
    technicalLabel->setObjectName(QStringLiteral("metadataSubheading"));
    metadataLayout->addWidget(technicalLabel, 0, 0, Qt::AlignLeft | Qt::AlignVCenter);

    auto *detailsLabel = new QLabel(tr("Details"), metadataGroup);
    detailsLabel->setObjectName(QStringLiteral("metadataSubheading"));
    metadataLayout->addWidget(detailsLabel, 0, 1, Qt::AlignLeft | Qt::AlignVCenter);

    m_cameraLensMetadataCheckBox = new ExportCheckBox(
        tr("Camera && lens information"), metadataGroup);
    m_cameraLensMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_cameraLensMetadataCheckBox, 1, 0);

    m_copyrightCreatorMetadataCheckBox = new ExportCheckBox(
        tr("Copyright && creator"), metadataGroup);
    m_copyrightCreatorMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_copyrightCreatorMetadataCheckBox, 1, 1);

    m_captureSettingsMetadataCheckBox = new ExportCheckBox(
        tr("Capture settings"), metadataGroup);
    m_captureSettingsMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_captureSettingsMetadataCheckBox, 2, 0);

    m_descriptionKeywordsMetadataCheckBox = new ExportCheckBox(
        tr("Description && keywords"), metadataGroup);
    m_descriptionKeywordsMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_descriptionKeywordsMetadataCheckBox, 2, 1);

    m_captureDateTimeMetadataCheckBox = new ExportCheckBox(
        tr("Date && time captured"), metadataGroup);
    m_captureDateTimeMetadataCheckBox->setChecked(true);
    metadataLayout->addWidget(m_captureDateTimeMetadataCheckBox, 3, 0);

    auto *privacyLabel = new QLabel(tr("Privacy"), metadataGroup);
    privacyLabel->setObjectName(QStringLiteral("metadataSubheading"));
    metadataLayout->addWidget(privacyLabel, 4, 1,
                              Qt::AlignLeft | Qt::AlignBottom);
    metadataLayout->setRowMinimumHeight(4, 22);

    m_deviceIdentifiersMetadataCheckBox = new ExportCheckBox(
        tr("Device identifiers"), metadataGroup);
    m_deviceIdentifiersMetadataCheckBox->setChecked(false);
    metadataLayout->addWidget(m_deviceIdentifiersMetadataCheckBox, 5, 0);

    m_locationMetadataCheckBox = new ExportCheckBox(
        tr("Location information (GPS)"), metadataGroup);
    m_locationMetadataCheckBox->setChecked(false);
    metadataLayout->addWidget(m_locationMetadataCheckBox, 5, 1);

    auto *deviceIdentifiersHint = new QLabel(
        tr("May include camera or lens serial numbers"), metadataGroup);
    deviceIdentifiersHint->setObjectName(QStringLiteral("metadataHint"));
    deviceIdentifiersHint->setWordWrap(true);
    deviceIdentifiersHint->setContentsMargins(22, 0, 0, 0);
    metadataLayout->addWidget(deviceIdentifiersHint, 6, 0,
                              Qt::AlignLeft | Qt::AlignTop);

    auto *locationHint = new QLabel(
        tr("May reveal where the photo was taken"), metadataGroup);
    locationHint->setObjectName(QStringLiteral("metadataHint"));
    locationHint->setWordWrap(true);
    locationHint->setContentsMargins(22, 0, 0, 0);
    metadataLayout->addWidget(locationHint, 6, 1,
                              Qt::AlignLeft | Qt::AlignTop);

    rootLayout->addWidget(metadataGroup);

    const Settings rememberedSettings = loadRememberedExportSettings();
    m_formatCombo->setCurrentIndex(
        rememberedSettings.format == Format::Png ? 1 : 0);
    m_qualitySlider->setValue(rememberedSettings.quality);
    m_cameraLensMetadataCheckBox->setChecked(
        rememberedSettings.includeCameraLensInfo);
    m_captureSettingsMetadataCheckBox->setChecked(
        rememberedSettings.includeCaptureSettings);
    m_captureDateTimeMetadataCheckBox->setChecked(
        rememberedSettings.includeCaptureDateTime);
    m_locationMetadataCheckBox->setChecked(
        rememberedSettings.includeLocationInfo);
    m_copyrightCreatorMetadataCheckBox->setChecked(
        rememberedSettings.includeCopyrightCreator);
    m_descriptionKeywordsMetadataCheckBox->setChecked(
        rememberedSettings.includeDescriptionKeywords);
    m_deviceIdentifiersMetadataCheckBox->setChecked(
        rememberedSettings.includeDeviceIdentifiers);
    m_subfolderEdit->setText(rememberedSettings.subfolderName);
    m_customFolder = rememberedSettings.customFolder;

    switch (rememberedSettings.locationMode) {
    case LocationMode::OriginalSubfolder:
        m_locationCombo->setCurrentIndex(1);
        break;
    case LocationMode::CustomFolder:
        m_locationCombo->setCurrentIndex(2);
        if (!m_customFolder.isEmpty()) {
            m_locationCombo->setEditText(QDir::toNativeSeparators(m_customFolder));
        }
        break;
    case LocationMode::SameAsOriginal:
        m_locationCombo->setCurrentIndex(0);
        break;
    }
    m_locationCombo->lineEdit()->setCursorPosition(0);
    m_locationCombo->lineEdit()->deselect();
    qualityValueLabel->setText(QStringLiteral("%1%").arg(m_qualitySlider->value()));
    qualityValueLabel->setEnabled(m_formatCombo->currentIndex() == 0);

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
    auto *doneButton = buttons->addButton(tr("Done"), QDialogButtonBox::ActionRole);
    connect(doneButton, &QPushButton::clicked, this, [this]() {
        saveRememberedExportSettings(settings());
        reject();
    });
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
                QTimer::singleShot(0, this, [this, index]() {
                    m_locationCombo->lineEdit()->setCursorPosition(0);
                    m_locationCombo->lineEdit()->deselect();
                    if (index == 2 && m_locationCombo->currentIndex() == 2) {
                        chooseCustomFolder();
                    }
                });
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
        "QLabel#metadataSubheading {"
        "  color: #dddddd;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "}"
        "QLabel#metadataHint {"
        "  color: #858585;"
        "  font-size: 10px;"
        "}"
        "QLabel:disabled { color: #676767; }"
        "QCheckBox { color: #cfcfcf; spacing: 8px; }"
        "QCheckBox::indicator {"
        "  width: 14px;"
        "  height: 14px;"
        "  border: 1px solid #6f6f6f;"
        "  border-radius: 3px;"
        "  background: #181818;"
        "}"
        "QCheckBox::indicator:hover {"
        "  border-color: #9d9d9d;"
        "  background: #202020;"
        "}"
        "QCheckBox::indicator:checked {"
        "  background: #e3e3e3;"
        "  border-color: #e3e3e3;"
        "}"
        "QCheckBox::indicator:checked:hover {"
        "  background: #f2f2f2;"
        "  border-color: #f2f2f2;"
        "}"
        "QCheckBox::indicator:disabled {"
        "  border-color: #4a4a4a;"
        "  background: #181818;"
        "}"
        "QCheckBox::indicator:checked:disabled {"
        "  border-color: #777777;"
        "  background: #777777;"
        "}"
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
    result.includeCameraLensInfo = m_cameraLensMetadataCheckBox->isChecked();
    result.includeCaptureSettings = m_captureSettingsMetadataCheckBox->isChecked();
    result.includeCaptureDateTime = m_captureDateTimeMetadataCheckBox->isChecked();
    result.includeLocationInfo = m_locationMetadataCheckBox->isChecked();
    result.includeCopyrightCreator = m_copyrightCreatorMetadataCheckBox->isChecked();
    result.includeDescriptionKeywords = m_descriptionKeywordsMetadataCheckBox->isChecked();
    result.includeDeviceIdentifiers = m_deviceIdentifiersMetadataCheckBox->isChecked();
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
