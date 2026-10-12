#pragma once

#include <QWidget>

#include <QStringList>

class QListWidget;
class QToolButton;

class HistoryPanel final : public QWidget
{
    Q_OBJECT

public:
    explicit HistoryPanel(QWidget *parent = nullptr);

    void setEntries(const QStringList &labels, int currentIndex);
    void setCurrentIndex(int currentIndex);

signals:
    void entryActivated(int index);

private:
    void setExpanded(bool expanded);

    QToolButton *m_headerButton = nullptr;
    QListWidget *m_list = nullptr;
    bool m_expanded = true;
};
