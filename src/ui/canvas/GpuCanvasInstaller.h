#pragma once

class QWidget;

class GpuCanvasInstaller final
{
public:
    GpuCanvasInstaller() = delete;

    [[nodiscard]] static bool install(QWidget *root);
};
