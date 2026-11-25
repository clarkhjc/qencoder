#pragma once

#include "core/TranscodeTask.hpp"

#include <QAbstractTableModel>
#include <QHash>
#include <QList>

// 任务表模型：只保存队列任务的副本，并通过 id -> 行号映射做增量更新。
// 这样数千行时视图无需为每行创建 QWidget，也避免直接引用 TaskQueue 的内部列表。
class TaskTableModel : public QAbstractTableModel
{
    Q_OBJECT
public:
    enum Column {
        StateColumn = 0,
        TaskColumn,
        ProgressColumn,
        TimeSpeedColumn,
        OutputColumn,
        ActionColumn,
        ColumnCount
    };

    enum Roles {
        TaskIdRole = Qt::UserRole,
        StateRole,
        ProgressRole,
        ElapsedMsRole,
        SpeedRole,
        ErrorRole
    };

    explicit TaskTableModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    int columnCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QVariant headerData(int section, Qt::Orientation orientation,
                        int role = Qt::DisplayRole) const override;
    Qt::ItemFlags flags(const QModelIndex &index) const override;

public slots:
    void addTask(TranscodeTask task);
    void updateState(const QString &id, TranscodeTask::State state);
    void updateProgress(const QString &id, double pct, qint64 elapsedMs, double speed);
    void setError(const QString &id, const QString &error);
    void removeTask(const QString &id);
    void clear();

private:
    struct Row {
        TranscodeTask task;
        QString fileName;
        QString error;
        qint64 elapsedMs = 0;
        double speed = 0.0;
        bool sourceMissing = false;
    };

    int rowOf(const QString &id) const;
    QString stateText(TranscodeTask::State state) const;
    QString timeSpeedText(const Row &row) const;

    QList<Row> m_rows;
    QHash<QString, int> m_rowById;
};
