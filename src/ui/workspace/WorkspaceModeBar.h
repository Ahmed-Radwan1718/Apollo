#pragma once

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>
#include <QVBoxLayout>
#include <QWidget>

class WorkspaceModeBar final : public QWidget
{
public:
    explicit WorkspaceModeBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("workspaceModeBar"));
        setFixedHeight(42);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(8);
        layout->addStretch(1);

        m_libraryButton = new QPushButton(tr("Library"), this);
        m_libraryButton->setObjectName(QStringLiteral("libraryModeButton"));
        m_libraryButton->setCheckable(true);
        m_libraryButton->setFixedSize(80, 32);
        m_libraryButton->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(m_libraryButton);

        m_developButton = new QPushButton(tr("Develop"), this);
        m_developButton->setObjectName(QStringLiteral("developModeButton"));
        m_developButton->setCheckable(true);
        m_developButton->setChecked(true);
        m_developButton->setFixedSize(80, 32);
        m_developButton->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(m_developButton);

        auto *buttonGroup = new QButtonGroup(this);
        buttonGroup->setExclusive(true);
        buttonGroup->addButton(m_libraryButton);
        buttonGroup->addButton(m_developButton);

        layout->addStretch(1);

        setStyleSheet(QStringLiteral(
            "QWidget#workspaceModeBar {"
            "  background: #181818;"
            "  border-bottom: 1px solid #252525;"
            "}"
            "QPushButton#libraryModeButton, QPushButton#developModeButton {"
            "  background: transparent;"
            "  border: 0;"
            "  color: #b5b5b5;"
            "  font-family: \"Segoe UI Variable\";"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "  padding: 0;"
            "}"
            "QPushButton#libraryModeButton:hover, QPushButton#developModeButton:hover {"
            "  color: #e0e0e0;"
            "}"
            "QPushButton#libraryModeButton:checked, QPushButton#developModeButton:checked {"
            "  color: #ffffff;"
            "  font-weight: 700;"
            "}"));
    }

    QPushButton *libraryButton() const noexcept { return m_libraryButton; }
    QPushButton *developButton() const noexcept { return m_developButton; }

    void setLibraryMode(bool libraryMode)
    {
        m_libraryButton->setChecked(libraryMode);
        m_developButton->setChecked(!libraryMode);
    }

    static WorkspaceModeBar *install(QWidget *centerPanel)
    {
        if (!centerPanel) {
            return nullptr;
        }

        auto *layout = qobject_cast<QVBoxLayout *>(centerPanel->layout());
        if (!layout) {
            return nullptr;
        }

        auto *modeBar = new WorkspaceModeBar(centerPanel);
        layout->insertWidget(0, modeBar);
        return modeBar;
    }

private:
    QPushButton *m_libraryButton = nullptr;
    QPushButton *m_developButton = nullptr;
};
