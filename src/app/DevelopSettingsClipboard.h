#pragma once

#include <QString>

class QWidget;

class DevelopSettingsClipboard final
{
public:
    DevelopSettingsClipboard() = delete;

    static bool copyFromPhoto(const QString &filePath);
    [[nodiscard]] static bool canPaste();
    static bool pasteToPhoto(QWidget *windowRoot, const QString &filePath);
};
