#pragma once

#include <QDialog>
#include <QString>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSlider;
class QSpinBox;
class QWidget;

class ExportDialog final : public QDialog
{
public:
    enum class Format {
        Jpeg,
        Png,
    };

    enum class ResizeMode {
        Original,
        Percent50,
        Percent25,
        LongEdge,
    };

    enum class LocationMode {
        SameAsOriginal,
        OriginalSubfolder,
        CustomFolder,
    };

    struct Settings {
        Format format = Format::Jpeg;
        ResizeMode resizeMode = ResizeMode::Original;
        LocationMode locationMode = LocationMode::SameAsOriginal;
        int longEdgePixels = 2048;
        int quality = 95;
        bool includeMetadata = true;
        QString subfolderName;
        QString customFolder;
    };

    explicit ExportDialog(const QString &sourceDirectory,
                          QWidget *parent = nullptr);

    [[nodiscard]] Settings settings() const;

private:
    void updateControlStates();
    void chooseCustomFolder();

    QString m_sourceDirectory;
    QString m_customFolder;

    QComboBox *m_formatCombo = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QSlider *m_qualitySlider = nullptr;
    QSpinBox *m_qualitySpinBox = nullptr;
    QComboBox *m_locationCombo = nullptr;
    QLabel *m_subfolderLabel = nullptr;
    QLineEdit *m_subfolderEdit = nullptr;
    QLabel *m_folderLabel = nullptr;
    QWidget *m_folderRow = nullptr;
    QLineEdit *m_folderEdit = nullptr;
    QPushButton *m_browseButton = nullptr;

    QComboBox *m_sizeCombo = nullptr;
    QLabel *m_longEdgeLabel = nullptr;
    QSpinBox *m_longEdgeSpinBox = nullptr;

    QCheckBox *m_includeMetadataCheckBox = nullptr;
    QPushButton *m_exportButton = nullptr;
};
