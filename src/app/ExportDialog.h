#pragma once

#include <QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
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

    struct Settings {
        Format format = Format::Jpeg;
        ResizeMode resizeMode = ResizeMode::Original;
        int longEdgePixels = 2048;
        int quality = 95;
        bool includeMetadata = true;
    };

    explicit ExportDialog(QWidget *parent = nullptr);

    [[nodiscard]] Settings settings() const;

private:
    void updateControlStates();

    QComboBox *m_formatCombo = nullptr;
    QLabel *m_qualityLabel = nullptr;
    QSlider *m_qualitySlider = nullptr;
    QSpinBox *m_qualitySpinBox = nullptr;
    QComboBox *m_sizeCombo = nullptr;
    QLabel *m_longEdgeLabel = nullptr;
    QSpinBox *m_longEdgeSpinBox = nullptr;
    QCheckBox *m_includeMetadataCheckBox = nullptr;
};
