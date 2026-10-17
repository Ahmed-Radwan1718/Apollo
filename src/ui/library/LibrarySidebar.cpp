#include "ui/library/LibrarySidebar.h"

#include <QFileInfo>
#include <QLabel>
#include <QSizePolicy>
#include <QVBoxLayout>

namespace {

QLabel *makeSectionTitle(const QString &text, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(QStringLiteral("librarySectionTitle"));
    return label;
}

QLabel *makeItem(const QString &text, bool selected, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(selected
                             ? QStringLiteral("librarySelectedItem")
                             : QStringLiteral("libraryItem"));
    label->setFixedHeight(26);
    return label;
}

} // namespace

LibrarySidebar::LibrarySidebar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("librarySidebar"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 14, 14, 12);
    layout->setSpacing(4);

    layout->addWidget(makeSectionTitle(tr("Catalog"), this));
    layout->addWidget(makeItem(tr("All Photos"), true, this));
    layout->addWidget(makeItem(tr("Recently Added"), false, this));

    layout->addSpacing(14);
    layout->addWidget(makeSectionTitle(tr("Folders"), this));

    auto *folderContainer = new QWidget(this);
    folderContainer->setObjectName(QStringLiteral("libraryFolderContainer"));
    m_folderLayout = new QVBoxLayout(folderContainer);
    m_folderLayout->setContentsMargins(0, 0, 0, 0);
    m_folderLayout->setSpacing(2);
    layout->addWidget(folderContainer);

    layout->addSpacing(14);
    layout->addWidget(makeSectionTitle(tr("Collections"), this));
    layout->addStretch(1);

    setStyleSheet(QStringLiteral(
        "QWidget#librarySidebar, QWidget#libraryFolderContainer {"
        "  background: #181818;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QLabel#librarySectionTitle {"
        "  color: #d6d6d6;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  padding: 0 0 4px 0;"
        "}"
        "QLabel#libraryItem, QLabel#librarySelectedItem {"
        "  color: #b5b5b5;"
        "  font-size: 11px;"
        "  padding: 0 8px;"
        "  border-radius: 3px;"
        "}"
        "QLabel#librarySelectedItem {"
        "  background: #222222;"
        "  color: #f2f2f2;"
        "  font-weight: 600;"
        "}"));
}

void LibrarySidebar::addFolder(const QString &folderPath)
{
    if (folderPath.isEmpty() || m_folders.contains(folderPath) || !m_folderLayout) {
        return;
    }

    m_folders.insert(folderPath);

    QString displayName = QFileInfo(folderPath).fileName();
    if (displayName.isEmpty()) {
        displayName = folderPath;
    }

    auto *item = makeItem(displayName, false, this);
    item->setToolTip(folderPath);
    m_folderLayout->addWidget(item);
}
