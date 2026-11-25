#include "ui/TaskTableDelegates.hpp"
#include "ui/IconFactory.hpp"
#include "ui/TaskTableModel.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QStyle>
#include <QToolTip>

namespace {

QString formatTimeSpeed(qint64 elapsedMs, double speed)
{
    if (elapsedMs <= 0 && speed <= 0.001)
        return QStringLiteral("—");

    const qint64 secs = qMax<qint64>(0, elapsedMs / 1000);
    QString text = QStringLiteral("%1:%2")
                       .arg(secs / 60, 2, 10, QChar('0'))
                       .arg(secs % 60, 2, 10, QChar('0'));
    if (speed > 0.001)
        text += QStringLiteral(" · %1x").arg(speed, 0, 'f', 2);
    return text;
}

} // namespace

// ---------------------------------------------------------------- ProgressDelegate

ProgressDelegate::ProgressDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void ProgressDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                             const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.state &= ~QStyle::State_HasFocus;

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const double pct = qBound(0.0, index.data(TaskTableModel::ProgressRole).toDouble(), 100.0);
    const QRect content = opt.rect.adjusted(10, 0, -10, 0);
    constexpr int barHeight = 8;
    constexpr int pctWidth = 40;
    constexpr int gap = 8;

    const qreal barWidth = qMax(0, content.width() - pctWidth - gap);
    const QRectF trackRect(content.x(),
                           content.y() + (content.height() - barHeight) / 2.0,
                           barWidth, barHeight);
    if (trackRect.width() <= 0)
        return;

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(Qt::NoPen);

    QColor trackColor = opt.palette.color(QPalette::Mid);
    trackColor.setAlpha(80);
    painter->setBrush(trackColor);
    painter->drawRoundedRect(trackRect, barHeight / 2.0, barHeight / 2.0);

    if (pct > 0.0) {
        QColor chunkColor = opt.palette.color(QPalette::Highlight);
        if (!chunkColor.isValid())
            chunkColor = QColor("#4c8dff");
        QRectF fillRect = trackRect;
        fillRect.setWidth(trackRect.width() * pct / 100.0);
        painter->setBrush(chunkColor);
        painter->drawRoundedRect(fillRect, barHeight / 2.0, barHeight / 2.0);
    }

    const QRect textRect(content.right() - pctWidth + 1, content.y(), pctWidth,
                         content.height());
    const bool selected = opt.state & QStyle::State_Selected;
    painter->setPen(opt.palette.color(selected ? QPalette::HighlightedText : QPalette::Text));
    QFont font = opt.font;
    if (font.pointSizeF() > 0)
        font.setPointSizeF(qMax(8.0, font.pointSizeF() - 1.0));
    painter->setFont(font);
    painter->drawText(textRect, Qt::AlignRight | Qt::AlignVCenter,
                      QStringLiteral("%1%").arg(static_cast<int>(pct)));
    painter->restore();
}

// ---------------------------------------------------------------- TimeSpeedDelegate

TimeSpeedDelegate::TimeSpeedDelegate(QObject *parent) : QStyledItemDelegate(parent) {}

void TimeSpeedDelegate::initStyleOption(QStyleOptionViewItem *option,
                                        const QModelIndex &index) const
{
    QStyledItemDelegate::initStyleOption(option, index);
    if (!index.isValid())
        return;
    option->text = formatTimeSpeed(index.data(TaskTableModel::ElapsedMsRole).toLongLong(),
                                   index.data(TaskTableModel::SpeedRole).toDouble());
}

// ---------------------------------------------------------------- ActionDelegate

ActionDelegate::ActionDelegate(QObject *parent) : QStyledItemDelegate(parent)
{
    m_view = qobject_cast<QAbstractItemView *>(parent);
    if (!m_view)
        return;
    m_view->setMouseTracking(true);
    m_view->viewport()->setMouseTracking(true);
    m_view->viewport()->installEventFilter(this);
}

void ActionDelegate::paint(QPainter *painter, const QStyleOptionViewItem &option,
                           const QModelIndex &index) const
{
    QStyleOptionViewItem opt = option;
    initStyleOption(&opt, index);
    opt.text.clear();
    opt.state &= ~QStyle::State_HasFocus;

    QStyle *style = opt.widget ? opt.widget->style() : QApplication::style();
    style->drawControl(QStyle::CE_ItemViewItem, &opt, painter, opt.widget);

    const auto state = index.data(TaskTableModel::StateRole).value<TranscodeTask::State>();
    const QVector<int> actions = actionsForState(state);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    for (int action : actions) {
        const QRect rect = buttonRect(opt.rect, state, action);
        if (rect.isEmpty())
            continue;

        if (m_hoverAction == action && m_hoverIndex.isValid() && m_hoverIndex == index) {
            QColor hover = opt.palette.color(QPalette::Highlight);
            hover.setAlpha(50);
            painter->setPen(Qt::NoPen);
            painter->setBrush(hover);
            painter->drawRoundedRect(rect, 7, 7);
        }

        const QIcon icon = IconFactory::icon(iconName(action));
        if (!icon.isNull())
            icon.paint(painter, rect.adjusted(6, 6, -6, -6), Qt::AlignCenter);
    }
    painter->restore();
}

bool ActionDelegate::helpEvent(QHelpEvent *event, QAbstractItemView *view,
                               const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event->type() == QEvent::ToolTip && index.isValid()
        && index.column() == TaskTableModel::ActionColumn) {
        const QRect cellRect = view ? view->visualRect(index) : option.rect;
        const int action = actionAt(index, cellRect, event->pos());
        if (action >= 0) {
            QToolTip::showText(event->globalPos(), toolTip(action), view);
            return true;
        }
    }
    return QStyledItemDelegate::helpEvent(event, view, option, index);
}

bool ActionDelegate::editorEvent(QEvent *event, QAbstractItemModel *model,
                                 const QStyleOptionViewItem &option, const QModelIndex &index)
{
    if (event->type() == QEvent::MouseButtonRelease && index.isValid()
        && index.column() == TaskTableModel::ActionColumn) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        if (mouseEvent->button() == Qt::LeftButton) {
            const QRect cellRect = m_view ? m_view->visualRect(index) : option.rect;
            const int action = actionAt(index, cellRect, mouseEvent->position().toPoint());
            if (action >= 0) {
                emit actionTriggered(index.data(TaskTableModel::TaskIdRole).toString(), action);
                return true;
            }
        }
    }
    return QStyledItemDelegate::editorEvent(event, model, option, index);
}

bool ActionDelegate::eventFilter(QObject *watched, QEvent *event)
{
    if (!m_view || watched != m_view->viewport())
        return QStyledItemDelegate::eventFilter(watched, event);

    if (event->type() == QEvent::MouseMove) {
        auto *mouseEvent = static_cast<QMouseEvent *>(event);
        const QPoint pos = mouseEvent->position().toPoint();
        const QModelIndex index = m_view->indexAt(pos);

        QPersistentModelIndex newHoverIndex;
        int newHoverAction = -1;
        if (index.isValid() && index.column() == TaskTableModel::ActionColumn) {
            const int action = actionAt(index, m_view->visualRect(index), pos);
            if (action >= 0) {
                newHoverIndex = index;
                newHoverAction = action;
            }
        }

        if (newHoverIndex != m_hoverIndex || newHoverAction != m_hoverAction) {
            m_hoverIndex = newHoverIndex;
            m_hoverAction = newHoverAction;
            m_view->viewport()->update();
        }
        m_view->viewport()->setCursor(newHoverAction >= 0 ? Qt::PointingHandCursor
                                                          : Qt::ArrowCursor);
    } else if (event->type() == QEvent::Leave) {
        if (m_hoverIndex.isValid() || m_hoverAction >= 0) {
            m_hoverIndex = QPersistentModelIndex();
            m_hoverAction = -1;
            m_view->viewport()->update();
        }
        m_view->viewport()->unsetCursor();
    }
    return QStyledItemDelegate::eventFilter(watched, event);
}

QVector<int> ActionDelegate::actionsForState(TranscodeTask::State state) const
{
    switch (state) {
    case TranscodeTask::State::Waiting:
        return {Start, Stop, Remove};
    case TranscodeTask::State::Running:
        return {Pause, Stop};
    case TranscodeTask::State::Paused:
        return {Resume, Stop};
    case TranscodeTask::State::Done:
    case TranscodeTask::State::Failed:
    case TranscodeTask::State::Cancelled:
        return {Remove};
    }
    return {};
}

QRect ActionDelegate::buttonRect(const QRect &cellRect, TranscodeTask::State state,
                                 int action) const
{
    const QVector<int> actions = actionsForState(state);
    const int index = actions.indexOf(action);
    if (index < 0)
        return {};

    constexpr int buttonSize = 28;
    constexpr int spacing = 2;
    const int totalWidth = actions.size() * buttonSize + (actions.size() - 1) * spacing;
    const int x = cellRect.x() + (cellRect.width() - totalWidth) / 2
                  + index * (buttonSize + spacing);
    const int y = cellRect.y() + (cellRect.height() - buttonSize) / 2;
    return QRect(x, y, buttonSize, buttonSize);
}

int ActionDelegate::actionAt(const QModelIndex &index, const QRect &cellRect,
                             const QPoint &pos) const
{
    if (!index.isValid())
        return -1;
    const auto state = index.data(TaskTableModel::StateRole).value<TranscodeTask::State>();
    for (int action : actionsForState(state)) {
        if (buttonRect(cellRect, state, action).contains(pos))
            return action;
    }
    return -1;
}

QString ActionDelegate::iconName(int action) const
{
    switch (action) {
    case Start: return QStringLiteral("play");
    case Pause: return QStringLiteral("pause");
    case Resume: return QStringLiteral("resume");
    case Stop: return QStringLiteral("stop");
    case Remove: return QStringLiteral("trash");
    default: return {};
    }
}

QString ActionDelegate::toolTip(int action) const
{
    switch (action) {
    case Start: return tr("启动");
    case Pause: return tr("暂停");
    case Resume: return tr("恢复");
    case Stop: return tr("停止");
    case Remove: return tr("移除");
    default: return {};
    }
}
