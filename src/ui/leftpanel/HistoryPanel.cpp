#include "ui/leftpanel/HistoryPanel.h"

#include <QAbstractItemView>
#include <QAbstractScrollArea>
#include <QEasingCurve>
#include <QKeySequence>
#include <QListWidget>
#include <QPainter>
#include <QPaintEvent>
#include <QResizeEvent>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace {

constexpr int kPanelAnimationDurationMs = 220;

class HistoryHeaderButton final : public QToolButton
{
public:
    explicit HistoryHeaderButton(QWidget *parent = nullptr)
        : QToolButton(parent)
        , m_animation(new QVariantAnimation(this))
    {
        setText(tr("History"));
        setCursor(Qt::PointingHandCursor);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(30);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

        m_animation->setDuration(kPanelAnimationDurationMs);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);
        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    m_angle = value.toReal();
                    update();
                });
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded) {
            return;
        }

        m_expanded = expanded;
        m_animation->stop();
        m_animation->setStartValue(m_angle);
        m_animation->setEndValue(expanded ? 90.0 : 0.0);
        m_animation->start();
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
        painter.rotate(m_angle);
        painter.drawLine(QPointF(-1.5, -3.5), QPointF(2.0, 0.0));
        painter.drawLine(QPointF(2.0, 0.0), QPointF(-1.5, 3.5));
    }

private:
    bool m_expanded = false;
    qreal m_angle = 0.0;
    QVariantAnimation *m_animation = nullptr;
};

class HistoryRevealContainer final : public QWidget
{
public:
    explicit HistoryRevealContainer(QWidget *parent = nullptr)
        : QWidget(parent)
        , m_animation(new QVariantAnimation(this))
    {
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

        m_animation->setDuration(kPanelAnimationDurationMs);
        m_animation->setEasingCurve(QEasingCurve::InOutCubic);

        connect(m_animation, &QVariantAnimation::valueChanged, this,
                [this](const QVariant &value) {
                    updateContentGeometry(value.toInt());
                });

        connect(m_animation, &QVariantAnimation::finished, this,
                [this]() {
                    updateContentGeometry(m_expanded ? height() : 0);
                    if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(m_content)) {
                        scrollArea->setVerticalScrollBarPolicy(
                            m_expanded ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
                    }
                });
    }

    void setContentWidget(QWidget *content)
    {
        m_content = content;
        if (!m_content) {
            return;
        }

        m_content->setParent(this);
        m_content->show();
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(m_content)) {
            scrollArea->setVerticalScrollBarPolicy(
                m_expanded ? Qt::ScrollBarAsNeeded : Qt::ScrollBarAlwaysOff);
        }
        updateContentGeometry(m_expanded ? height() : 0);
    }

    void setExpanded(bool expanded)
    {
        if (m_expanded == expanded || !m_content) {
            return;
        }

        m_expanded = expanded;
        m_animation->stop();
        if (auto *scrollArea = qobject_cast<QAbstractScrollArea *>(m_content)) {
            scrollArea->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        }
        m_animation->setStartValue(m_content->height());
        m_animation->setEndValue(expanded ? height() : 0);
        m_animation->start();
    }

protected:
    void resizeEvent(QResizeEvent *event) override
    {
        QWidget::resizeEvent(event);

        if (!m_content) {
            return;
        }

        if (m_animation->state() == QAbstractAnimation::Running) {
            updateContentGeometry(qMin(m_content->height(), height()));
        } else {
            updateContentGeometry(m_expanded ? height() : 0);
        }
    }

private:
    void updateContentGeometry(int contentHeight)
    {
        if (!m_content) {
            return;
        }

        const int clampedHeight = qBound(0, contentHeight, height());
        m_content->setGeometry(0, 0, width(), clampedHeight);
    }

    QWidget *m_content = nullptr;
    QVariantAnimation *m_animation = nullptr;
    bool m_expanded = false;
};

} // namespace

HistoryPanel::HistoryPanel(QWidget *parent)
    : QWidget(parent)
    , m_expanded(false)
{
    setObjectName(QStringLiteral("historyPanel"));
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 0, 14, 12);
    layout->setSpacing(2);

    auto *header = new HistoryHeaderButton(this);
    m_headerButton = header;
    layout->addWidget(m_headerButton);

    auto *revealContainer = new HistoryRevealContainer(this);

    m_list = new QListWidget(revealContainer);
    m_list->setObjectName(QStringLiteral("historyList"));
    m_list->setSelectionMode(QAbstractItemView::SingleSelection);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setFocusPolicy(Qt::NoFocus);
    revealContainer->setContentWidget(m_list);
    layout->addWidget(revealContainer, 1);

    setStyleSheet(QStringLiteral(
        "QWidget#historyPanel {"
        "  background: #181818;"
        "  border: 0;"
        "  border-top: 1px solid #2a2a2a;"
        "  font-family: \"Segoe UI Variable\";"
        "}"
        "QListWidget#historyList {"
        "  background: transparent;"
        "  border: 0;"
        "  outline: 0;"
        "  color: #adadad;"
        "  font-size: 11px;"
        "}"
        "QListWidget#historyList::item {"
        "  min-height: 25px;"
        "  padding: 0 6px;"
        "  border-radius: 3px;"
        "}"
        "QListWidget#historyList::item:hover {"
        "  background: #222222;"
        "  color: #dedede;"
        "}"
        "QListWidget#historyList::item:selected {"
        "  background: #2d2d2d;"
        "  color: #f0f0f0;"
        "}"));

    connect(m_headerButton, &QToolButton::clicked, this,
            [this]() { setExpanded(!m_expanded); });

    connect(m_list, &QListWidget::itemClicked, this,
            [this](QListWidgetItem *item) {
                const int row = m_list->row(item);
                if (row >= 0) {
                    emit entryActivated(row);
                }
            });

    QWidget *shortcutParent = window();
    if (!shortcutParent) {
        shortcutParent = this;
    }

    auto *undoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Z")),
                                       shortcutParent);
    undoShortcut->setContext(Qt::WindowShortcut);
    connect(undoShortcut, &QShortcut::activated,
            this, &HistoryPanel::undo);

    auto *redoShortcut = new QShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+Z")),
                                       shortcutParent);
    redoShortcut->setContext(Qt::WindowShortcut);
    connect(redoShortcut, &QShortcut::activated,
            this, &HistoryPanel::redo);
}

void HistoryPanel::setEntries(const QStringList &labels, int currentIndex)
{
    m_list->clear();
    for (const QString &label : labels) {
        m_list->addItem(label);
    }
    setCurrentIndex(currentIndex);
}

void HistoryPanel::setCurrentIndex(int currentIndex)
{
    if (currentIndex < 0 || currentIndex >= m_list->count()) {
        m_list->clearSelection();
        m_list->setCurrentRow(-1);
        return;
    }

    m_list->setCurrentRow(currentIndex);
    m_list->scrollToItem(m_list->item(currentIndex), QAbstractItemView::EnsureVisible);
}

void HistoryPanel::undo()
{
    const int currentRow = m_list->currentRow();
    if (currentRow > 0) {
        emit entryActivated(currentRow - 1);
    }
}

void HistoryPanel::redo()
{
    const int currentRow = m_list->currentRow();
    if (currentRow >= 0 && currentRow + 1 < m_list->count()) {
        emit entryActivated(currentRow + 1);
    }
}

bool HistoryPanel::canUndo() const noexcept
{
    return m_list && m_list->currentRow() > 0;
}

bool HistoryPanel::canRedo() const noexcept
{
    return m_list
        && m_list->currentRow() >= 0
        && m_list->currentRow() + 1 < m_list->count();
}

void HistoryPanel::setExpanded(bool expanded)
{
    if (m_expanded == expanded) {
        return;
    }

    m_expanded = expanded;
    static_cast<HistoryHeaderButton *>(m_headerButton)->setExpanded(expanded);

    if (m_list && m_list->parentWidget()) {
        static_cast<HistoryRevealContainer *>(m_list->parentWidget())->setExpanded(expanded);
    }
}
