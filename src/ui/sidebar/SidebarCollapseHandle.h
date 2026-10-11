#pragma once

#include <QEasingCurve>
#include <QEvent>
#include <QList>
#include <QPointer>
#include <QToolButton>
#include <QVariantAnimation>
#include <QWidget>

class SidebarCollapseHandle final : public QToolButton
{
public:
    enum class Side {
        Left,
        Right,
    };

    explicit SidebarCollapseHandle(QWidget *target, Side side)
        : QToolButton(target)
        , m_target(target)
        , m_side(side)
        , m_expandedWidth(target ? target->width() : 0)
    {
        if (!m_target || m_expandedWidth <= kCollapsedWidth) {
            hide();
            return;
        }

        setObjectName(QStringLiteral("sidebarCollapseHandle"));
        setFocusPolicy(Qt::NoFocus);
        setCursor(Qt::PointingHandCursor);
        setFixedSize(kHandleWidth, kHandleHeight);
        setToolButtonStyle(Qt::ToolButtonTextOnly);
        setStyleSheet(QStringLiteral(
            "QToolButton#sidebarCollapseHandle {"
            "  background: #1d1d1d;"
            "  border: 1px solid #303030;"
            "  border-radius: 3px;"
            "  color: #8f8f8f;"
            "  font-size: 16px;"
            "  padding: 0;"
            "}"
            "QToolButton#sidebarCollapseHandle:hover {"
            "  background: #292929;"
            "  color: #d0d0d0;"
            "}"
            "QToolButton#sidebarCollapseHandle:pressed {"
            "  background: #333333;"
            "  color: #eeeeee;"
            "}"));

        m_animation = new QVariantAnimation(this);
        m_animation->setDuration(kAnimationDurationMs);
        m_animation->setEasingCurve(QEasingCurve::OutCubic);

        connect(m_animation, &QVariantAnimation::valueChanged,
                this, [this](const QVariant &value) {
                    if (!m_target) {
                        return;
                    }

                    const int width = value.toInt();
                    m_target->setMinimumWidth(width);
                    m_target->setMaximumWidth(width);
                    reposition();
                });

        connect(m_animation, &QVariantAnimation::finished,
                this, [this]() {
                    if (m_collapsed) {
                        hideTargetChildren();
                    }
                    reposition();
                });

        connect(this, &QToolButton::clicked,
                this, [this]() {
                    setCollapsed(!m_collapsed);
                });

        m_target->installEventFilter(this);
        updateAppearance();
        reposition();
        raise();
    }

    ~SidebarCollapseHandle() override
    {
        if (m_target) {
            m_target->removeEventFilter(this);
        }
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == m_target
            && (event->type() == QEvent::Resize || event->type() == QEvent::Show)) {
            reposition();
            raise();
        }

        return QToolButton::eventFilter(watched, event);
    }

private:
    static constexpr int kCollapsedWidth = 22;
    static constexpr int kHandleWidth = 20;
    static constexpr int kHandleHeight = 46;
    static constexpr int kAnimationDurationMs = 180;

    void setCollapsed(bool collapsed)
    {
        if (!m_target || m_collapsed == collapsed) {
            return;
        }

        m_collapsed = collapsed;
        m_animation->stop();

        if (!m_collapsed) {
            restoreTargetChildren();
        }

        updateAppearance();
        m_animation->setStartValue(m_target->width());
        m_animation->setEndValue(m_collapsed ? kCollapsedWidth : m_expandedWidth);
        m_animation->start();
    }

    void hideTargetChildren()
    {
        if (!m_target) {
            return;
        }

        m_hiddenChildren.clear();
        const QList<QWidget *> children =
            m_target->findChildren<QWidget *>(QString(), Qt::FindDirectChildrenOnly);
        for (QWidget *child : children) {
            if (child == this || !child->isVisible()) {
                continue;
            }

            m_hiddenChildren.append(child);
            child->hide();
        }

        raise();
    }

    void restoreTargetChildren()
    {
        for (const QPointer<QWidget> &child : m_hiddenChildren) {
            if (child) {
                child->show();
            }
        }
        m_hiddenChildren.clear();
        raise();
    }

    void updateAppearance()
    {
        const bool left = m_side == Side::Left;
        const QString chevron = left
            ? (m_collapsed ? QStringLiteral("›") : QStringLiteral("‹"))
            : (m_collapsed ? QStringLiteral("‹") : QStringLiteral("›"));
        setText(chevron);

        const QString sideName = left ? tr("left") : tr("right");
        setToolTip(m_collapsed
                       ? tr("Expand %1 panel").arg(sideName)
                       : tr("Collapse %1 panel").arg(sideName));
    }

    void reposition()
    {
        if (!m_target) {
            return;
        }

        const int y = qMax(0, (m_target->height() - height()) / 2);
        const int x = m_side == Side::Left
            ? qMax(0, m_target->width() - width())
            : 0;
        move(x, y);
    }

    QPointer<QWidget> m_target;
    Side m_side;
    int m_expandedWidth = 0;
    bool m_collapsed = false;
    QVariantAnimation *m_animation = nullptr;
    QList<QPointer<QWidget>> m_hiddenChildren;
};
