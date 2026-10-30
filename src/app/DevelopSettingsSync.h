#pragma once

#include <QString>
#include <QStringList>

class QWidget;

class DevelopSettingsSync final
{
public:
    DevelopSettingsSync() = delete;

    [[nodiscard]] static bool run(QWidget *windowRoot,
                                  const QString &sourceFilePath,
                                  const QStringList &selectedFilePaths);
};
