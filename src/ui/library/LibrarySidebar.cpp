#include "ui/library/LibrarySidebar.h"

#include <QFileInfo>
#include <QFont>
#include <QLabel>
#include <QPainter>
#include <QPaintEvent>
#include <QPen>
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

class LibrarySectionHeader final : public QToolButton
{
public:
    LibrarySectionHeader(const QString &text,
                         bool expanded,
                         QWidget *parent = nullptr)
        : QToolButton(parent)
    {
        setText(text);
        setCheckable(true);
        setChecked(expanded);
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(30);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    }

protected:
    void paintEvent(QPaintEvent *event) override
    {
        Q_UNUSED(event);

        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::TextAntialiasing, true);

        const QColor color = underMouse()
            ? QColor(255, 255, 255)
            : QColor(214, 214, 214);
        painter.setPen(color);

        QFont titleFont = font();
        titleFont.setPointSizeF(9.0);
        titleFont.setWeight(QFont::DemiBold);
        painter.setFont(titleFont);
        painter.drawText(rect().adjusted(0, 0, -24, 0),
                         Qt::AlignLeft | Qt::AlignVCenter,
                         text());

        painter.setPen(QPen(color, 1.35, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        const QPointF center(width() - 10.0, height() / 2.0);
        painter.translate(center);
        if (isChecked()) {
            painter.rotate(90.0);
        }
        painter.drawLine(QPointF(-1.5, -3.5), QPointF(2.0, 0.0));
        painter.drawLine(QPointF(2.0, 0.0), QPointF(-1.5, 3.5));
    }
};

LibrarySectionHeader *makeSectionHeader(const QString &text,
                                        QWidget *content,
                                        bool expanded,
                                        QWidget *parent)
{
    auto *button = new LibrarySectionHeader(text, expanded, parent);
    content->setVisible(expanded);

    QObject::connect(button, &QToolButton::toggled, content,
                     [content](bool isExpanded) {
                         content->setVisible(isExpanded);
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
    layout->setContentsMargins(14, 0, 14, 12);
    layout->setSpacing(2);

    auto *catalogContent = makeSectionContent(this);
    auto *catalogLayout = new QVBoxLayout(catalogContent);
    catalogLayout->setContentsMargins(0, 0, 0, 0);
    catalogLayout->setSpacing(2);
    catalogLayout->addWidget(makeItem(tr("All Photos"), true, catalogContent));
    catalogLayout->addWidget(makeItem(tr("Recently Added"), false, catalogContent));
    catalogLayout->addWidget(makeItem(tr("Favorites"), false, catalogContent));

    layout->addWidget(makeSectionHeader(tr("Catalog"), catalogContent, false, this));
    layout->addWidget(catalogContent);

    auto *folderContainer = makeSectionContent(this);
    folderContainer->setObjectName(QStringLiteral("libraryFolderContainer"));
    m_folderLayout = new QVBoxLayout(folderContainer);
    m_folderLayout->setContentsMargins(0, 0, 0, 0);
    m_folderLayout->setSpacing(2);

    layout->addWidget(makeSectionHeader(tr("Folders"), folderContainer, true, this));
    layout->addWidget(folderContainer);

    auto *collectionsContent = makeSectionContent(this);
    auto *collectionsLayout = new QVBoxLayout(collectionsContent);
    collectionsLayout->setContentsMargins(0, 0, 0, 0);
    collectionsLayout->setSpacing(2);

    layout->addWidget(makeSectionHeader(tr("Collections"), collectionsContent, false, this));
    layout->addWidget(collectionsContent);
    layout->addStretch(1);

    setStyleSheet(QStringLiteral(
        "QWidget#librarySidebar, QWidget#libraryFolderContainer,"
        "QWidget#librarySectionContent {"
        "  background: #181818;"
        "  border: 0;"
        "  font-family: \"Segoe UI Variable\";"
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
