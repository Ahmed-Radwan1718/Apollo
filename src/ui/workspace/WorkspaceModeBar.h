#pragma once

#include <QHBoxLayout>
#include <QPushButton>
#include <QSizePolicy>
#include <QVBoxLayout>
#include <QWidget>

class WorkspaceModeBar final : public QWidget
{
public:
    explicit WorkspaceModeBar(QWidget *parent = nullptr)
        : QWidget(parent)
    {
        setObjectName(QStringLiteral("workspaceModeBar"));
        setFixedHeight(46);

        auto *layout = new QHBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);
        layout->addStretch(1);

        auto *buttonRegion = new QWidget(this);
        buttonRegion->setObjectName(QStringLiteral("workspaceModeButtonRegion"));
        buttonRegion->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);

        auto *buttonLayout = new QHBoxLayout(buttonRegion);
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->setSpacing(8);
        buttonLayout->addStretch(1);

        auto *libraryButton = new QPushButton(tr("Library"), buttonRegion);
        libraryButton->setObjectName(QStringLiteral("libraryModeButton"));
        libraryButton->setFixedSize(88, 34);
        libraryButton->setFocusPolicy(Qt::NoFocus);
        buttonLayout->addWidget(libraryButton);

        auto *developButton = new QPushButton(tr("Develop"), buttonRegion);
        developButton->setObjectName(QStringLiteral("developModeButton"));
        developButton->setProperty("activeMode", true);
        developButton->setFixedSize(88, 34);
        developButton->setFocusPolicy(Qt::NoFocus);
        buttonLayout->addWidget(developButton);

        buttonLayout->addStretch(1);
        layout->addWidget(buttonRegion, 1);

        setStyleSheet(QStringLiteral(
            "QWidget#workspaceModeBar {"
            "  background: #181818;"
            "  border-bottom: 1px solid #252525;"
            "}"
            "QWidget#workspaceModeButtonRegion {"
            "  background: transparent;"
            "}"
            "QPushButton#libraryModeButton, QPushButton#developModeButton {"
            "  background: transparent;"
            "  border: 1px solid transparent;"
            "  border-radius: 4px;"
            "  color: #c7c7c7;"
            "  font-family: \"Segoe UI Variable\";"
            "  font-size: 13px;"
            "  font-weight: 600;"
            "  padding: 0;"
            "}"
            "QPushButton#libraryModeButton:hover, QPushButton#developModeButton:hover {"
            "  background: #242424;"
            "  color: #f0f0f0;"
            "}"
            "QPushButton[activeMode=\"true\"] {"
            "  background: #303030;"
            "  border: 1px solid #4d4d4d;"
            "  color: #ffffff;"
            "  font-weight: 700;"
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
