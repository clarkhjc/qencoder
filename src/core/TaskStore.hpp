#pragma once

#include "core/TranscodeTask.hpp"

#include <QList>
#include <QSqlDatabase>
#include <QString>

// 任务队列的 SQLite 持久化。
// 只有在显式 open() 之后才会写库；未调用 open() 时队列保持纯内存模式（便于测试）。
class TaskStore
{
public:
    TaskStore() = default;
    ~TaskStore();

    TaskStore(const TaskStore &) = delete;
    TaskStore &operator=(const TaskStore &) = delete;

    // dbPath 为空时使用 QStandardPaths::AppDataLocation/tasks.db，
    // 可用环境变量 QENCODER_TASK_DB 覆盖。
    bool open(const QString &dbPath = QString());
    void close();
    bool isOpen() const;

    QList<TranscodeTask> loadAll() const;
    void upsert(const TranscodeTask &task);
    void remove(const QString &id);
    void clear();

    static QString defaultDatabasePath();

private:
    bool ensureSchema();

    QSqlDatabase m_db;
    QString m_connectionName;
};
