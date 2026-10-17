#pragma once

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
        setFixedHeight(40);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(4);
        layout->addStretch(1);

        auto *libraryButton = new QPushButton(tr("Library"), this);
        libraryButton->setObjectName(QStringLiteral("libraryModeButton"));
        libraryButton->setFixedSize(72, 30);
        libraryButton->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(libraryButton);

        auto *developButton = new QPushButton(tr("Develop"), this);
        developButton->setObjectName(QStringLiteral("developModeButton"));
        developButton->setProperty("activeMode", true);
        developButton->setFixedSize(72, 30);
        developButton->setFocusPolicy(Qt::NoFocus);
        layout->addWidget(developButton);

        layout->addStretch(1);

        setStyleSheet(QStringLiteral(
            "QWidget#workspaceModeBar {"
            "  background: #181818;"
            "  border-bottom: 1px solid #252525;"
            "}"
            "QPushButton#libraryModeButton, QPushButton#developModeButton {"
            "  background: transparent;"
            "  border: 0;"
            "  color: #9f9f9f;"
            "  font-family: \"Segoe UI Variable\";"
            "  font-size: 12px;"
            "  padding: 0;"
            "}"
            "QPushButton#libraryModeButton:hover, QPushButton#developModeButton:hover {"
            "  color: #d7d7d7;"
            "}"
            "QPushButton[activeMode=\"true\"] {"
            "  color: #f0f0f0;"
            "  font-weight: 600;"
            "}"));
    }

    static void install(QWidget *centerPanel)
    {
        if (!centerPanel) {
            return;
        }

        auto *layout = qobject_cast<QVBoxLayout *>(centerPanel->layout());
        if (!layout) {
            return;
        }

        layout->insertWidget(0, new WorkspaceModeBar(centerPanel));
    }
};
