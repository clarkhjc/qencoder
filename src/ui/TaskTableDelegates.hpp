#pragma once

#include "core/TranscodeTask.hpp"

#include <QPersistentModelIndex>
#include <QRect>
#include <QStyledItemDelegate>
#include <QVector>

class QAbstractItemView;
class QHelpEvent;

// 进度列：圆角进度条 + 右侧百分比
class ProgressDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit ProgressDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
};

// 耗时/速度列：mm:ss · 1.5x
class TimeSpeedDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit TimeSpeedDelegate(QObject *parent = nullptr);

    void initStyleOption(QStyleOptionViewItem *option,
                         const QModelIndex &index) const override;
};

// 操作列：按任务状态自绘 28x28 图标按钮，点击后发出 actionTriggered
class ActionDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    enum Action { Start = 0, Pause = 1, Resume = 2, Stop = 3, Remove = 4 };
    Q_ENUM(Action)

    explicit ActionDelegate(QObject *parent = nullptr);

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    bool helpEvent(QHelpEvent *event, QAbstractItemView *view,
                   const QStyleOptionViewItem &option, const QModelIndex &index) override;
    bool editorEvent(QEvent *event, QAbstractItemModel *model,
                     const QStyleOptionViewItem &option, const QModelIndex &index) override;

signals:
    void actionTriggered(const QString &id, int action);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QVector<int> actionsForState(TranscodeTask::State state) const;
    QRect buttonRect(const QRect &cellRect, TranscodeTask::State state, int action) const;
    int actionAt(const QModelIndex &index, const QRect &cellRect, const QPoint &pos) const;
    QString iconName(int action) const;
    QString toolTip(int action) const;

    QAbstractItemView *m_view = nullptr;
    QPersistentModelIndex m_hoverIndex;
    int m_hoverAction = -1;
};
