#pragma once

#include <QDialog>

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
};
