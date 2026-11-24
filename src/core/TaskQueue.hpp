#pragma once

#include "core/TaskStore.hpp"
#include "core/TranscodePreset.hpp"
#include "core/TranscodeTask.hpp"
#include "core/TranscodeWorker.hpp"

#include <QMap>
#include <QObject>

// 步骤 5：任务队列增强版
// - 并发：默认 maxParallel = idealThreadCount()-1，可在设置里调整（0 = 自动）
// - 全队列：pauseAll/resumeAll；单任务：pauseTask/resumeTask/cancelTask
// - 状态机：Waiting -> Running -> Done/Failed；Running -> Paused -> Running
class TaskQueue : public QObject
{
    Q_OBJECT
public:
    explicit TaskQueue(QObject *parent = nullptr);

    void addTask(TranscodeTask t);
    const QList<TranscodeTask> &tasks() const;
    bool removeTask(int index); // 仅移除等待中/已结束且无 worker 的任务
    bool isActive(const QString &id) const; // 该任务是否仍有 worker 在运行
    int indexOf(const QString &id) const;

    // 队列控制
    void start();                    // 启动队列（按 maxParallel 并发启动）
    void pauseAll();
    void resumeAll();
    void cancelAll();
    void clearAll();                 // 取消并删除所有任务

    // 新任务加入时是否自动启动（步骤 7：用户场景——添加后等待手动"启动队列"）
    void setAutoStart(bool on) { m_autoStart = on; }
    bool autoStart() const { return m_autoStart; }

    // 单任务控制
    void pauseTask(const QString &id);
    void resumeTask(const QString &id);
    void cancelTask(const QString &id);
    void startTask(const QString &id); // 等待中的任务：启动

    void setMaxParallel(int n); // n <= 0 表示自动（CPU-1）
    int maxParallel() const;

    // 打开数据库并加载上次的任务；dbPath 为空时使用默认位置。
    // 上次退出时仍在运行/暂停的任务会以进度 0 重新排队（ffmpeg 无法断点续转）。
    // 成功后所有新增/状态变更/删除都会自动写库。返回 false 表示数据库不可用。
    bool loadFromStore(const QString &dbPath = QString());

signals:
    void taskAdded(TranscodeTask t);
    void stateChanged(const QString &id, TranscodeTask::State state);
    void progress(const QString &id, double pct, qint64 elapsedMs, double speed);
    void taskFinished(const QString &id, TranscodeTask::State state, QString error);
    void notice(const QString &message); // 给状态栏的提示（如平台不支持暂停）
    void tasksCleared();                 // 全部任务已从队列删除

private:
    QString makeId();
    TranscodeTask *taskById(const QString &id);
    void launchNext();               // 若有空位且队列未暂停，启动一个
    void launchWorker(TranscodeTask *t); // 启动指定任务（Waiting -> Running）
    void setState(const QString &id, TranscodeTask::State state);
    int runningCount() const;

    QList<TranscodeTask> m_tasks;
    QMap<QString, TranscodeWorker *> m_workers; // 每任务一个 worker，完成后 deleteLater
    bool m_queuePaused = false;
    bool m_autoStart = true;
    int m_maxParallel = 0; // 0=动态计算
    qint64 m_nextId = 0;
    TaskStore m_store;
};
