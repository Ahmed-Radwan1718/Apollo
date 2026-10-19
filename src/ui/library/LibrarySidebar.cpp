#include "ui/library/LibrarySidebar.h"

#include <QFileInfo>
#include <QLabel>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

namespace {

QLabel *makeItem(const QString &text, bool selected, QWidget *parent)
{
    auto *label = new QLabel(text, parent);
    label->setObjectName(selected
                             ? QStringLiteral("librarySelectedItem")
                             : QStringLiteral("libraryItem"));
    label->setFixedHeight(26);
    return label;
}

QToolButton *makeSectionHeader(const QString &text,
                               QWidget *content,
                               QWidget *parent)
{
    auto *button = new QToolButton(parent);
    button->setObjectName(QStringLiteral("librarySectionHeader"));
    button->setText(text);
    button->setArrowType(Qt::DownArrow);
    button->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
    button->setCheckable(true);
    button->setChecked(true);
    button->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    QObject::connect(button, &QToolButton::toggled, content,
                     [button, content](bool expanded) {
                         content->setVisible(expanded);
                         button->setArrowType(expanded ? Qt::DownArrow : Qt::RightArrow);
                     });

    return button;
}

QWidget *makeSectionContent(QWidget *parent)
{
    auto *content = new QWidget(parent);
    content->setObjectName(QStringLiteral("librarySectionContent"));
    return content;
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

    auto *catalogContent = makeSectionContent(this);
    auto *catalogLayout = new QVBoxLayout(catalogContent);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(4);
    catalogLayout->addWidget(makeItem(tr("All Photos"), true, catalogContent));
    catalogLayout->addWidget(makeItem(tr("Recently Added"), false, catalogContent));

    layout->addWidget(makeSectionHeader(tr("Catalog"), catalogContent, this));
    layout->addWidget(catalogContent);

    layout->addSpacing(14);

    auto *folderContainer = makeSectionContent(this);
    folderContainer->setObjectName(QStringLiteral("libraryFolderContainer"));
    m_folderLayout = new QVBoxLayout(folderContainer);
    m_folderLayout->setContentsMargins(0, 0, 0, 0);
    m_folderLayout->setSpacing(2);

    layout->addWidget(makeSectionHeader(tr("Folders"), folderContainer, this));
    layout->addWidget(folderContainer);

    layout->addSpacing(14);

    auto *collectionsContent = makeSectionContent(this);
    auto *collectionsLayout = new QVBoxLayout(collectionsContent);
    collectionsLayout->setContentsMargins(0, 0, 0, 0);
    collectionsLayout->setSpacing(2);

    layout->addWidget(makeSectionHeader(tr("Collections"), collectionsContent, this));
    layout->addWidget(collectionsContent);
    layout->addStretch(1);

    setStyleSheet(QStringLiteral(
        "QWidget#librarySidebar, QWidget#libraryFolderContainer,"
        "QWidget#librarySectionContent {"
        "  background: #181818;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QToolButton#librarySectionHeader {"
        "  background: transparent;"
        "  border: 0;"
        "  color: #d6d6d6;"
        "  font-size: 12px;"
        "  font-weight: 600;"
        "  text-align: left;"
        "  padding: 0 0 4px 0;"
        "}"
        "QToolButton#librarySectionHeader:hover {"
        "  color: #f2f2f2;"
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
