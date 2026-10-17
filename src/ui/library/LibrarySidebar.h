#pragma once

#include <QSet>
#include <QString>
#include <QWidget>

class QVBoxLayout;

class LibrarySidebar final : public QWidget
{
public:
    explicit LibrarySidebar(QWidget *parent = nullptr);

    void addFolder(const QString &folderPath);

private:
    QVBoxLayout *m_folderLayout = nullptr;
    QSet<QString> m_folders;
};
