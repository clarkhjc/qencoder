#include "core/TaskQueue.hpp"
#include "presets/PresetManager.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QThread>

namespace {
int defaultParallel()
{
    int n = QThread::idealThreadCount();
    return qMax(1, n - 1);
}
} // namespace

TaskQueue::TaskQueue(QObject *parent) : QObject(parent)
{
    m_maxParallel = defaultParallel();
}

QString TaskQueue::makeId()
{
    QString id;
    do {
        id = QString::number(++m_nextId);
    } while (indexOf(id) >= 0);
    return id;
}

void TaskQueue::addTask(TranscodeTask t)
{
    if (t.id.isEmpty() || indexOf(t.id) >= 0)
        t.id = makeId();
    // 入队时固化预设快照：之后预设改名/删除不会影响已排队任务
    if (t.presetJson.isEmpty() && !t.presetName.isEmpty()) {
        const TranscodePreset p = PresetManager::presetByName(t.presetName);
        if (!p.name.isEmpty())
            t.presetJson = PresetManager::presetToJson(p);
    }
    m_tasks.append(t);
    m_store.upsert(t);
    emit taskAdded(t);
    if (m_autoStart)
        launchNext();
}

const QList<TranscodeTask> &TaskQueue::tasks() const
{
    return m_tasks;
}

bool TaskQueue::removeTask(int index)
{
    if (index < 0 || index >= m_tasks.size())
        return false;
    const QString id = m_tasks.at(index).id;
    if (m_workers.contains(id))
        return false; // 运行中/正在退出的任务需要先取消并等待结束
    m_tasks.removeAt(index);
    m_store.remove(id);
    return true;
}

bool TaskQueue::isActive(const QString &id) const
{
    return m_workers.contains(id);
}

int TaskQueue::indexOf(const QString &id) const
{
    for (int i = 0; i < m_tasks.size(); ++i)
        if (m_tasks.at(i).id == id)
            return i;
    return -1;
}

TranscodeTask *TaskQueue::taskById(const QString &id)
{
    for (TranscodeTask &t : m_tasks)
        if (t.id == id)
            return &t;
    return nullptr;
}

void TaskQueue::setState(const QString &id, TranscodeTask::State state)
{
    TranscodeTask *t = taskById(id);
    if (!t || t->state == state)
        return;
    t->state = state;
    if (state == TranscodeTask::State::Done)
        t->progress = 100.0; // 完成的任务入库时保存最终进度
    m_store.upsert(*t);
    emit stateChanged(id, state);
}

void TaskQueue::setMaxParallel(int n)
{
    m_maxParallel = n > 0 ? n : 0; // 0 = 自动（CPU-1）
}

int TaskQueue::maxParallel() const
{
    return m_maxParallel > 0 ? m_maxParallel : defaultParallel();
}

int TaskQueue::runningCount() const
{
    return m_workers.keys().size();
}

void TaskQueue::start()
{
    m_queuePaused = false;
    launchNext();
}

void TaskQueue::pauseAll()
{
    bool unsupported = false;
    bool anyPaused = false;
    const auto workers = m_workers.values();
    for (auto *w : workers) {
        const QString id = m_workers.key(w);
        if (w->pause()) {
            setState(id, TranscodeTask::State::Paused);
            anyPaused = true;
        } else {
            unsupported = true;
        }
    }
    // 只有确实暂停了任务才阻断队列，避免"平台不支持暂停"时队列被误停
    if (anyPaused)
        m_queuePaused = true;
    if (unsupported)
        emit notice(tr("当前平台不支持暂停正在运行的任务"));
}

void TaskQueue::resumeAll()
{
    m_queuePaused = false;
    const auto workers = m_workers.values();
    for (auto *w : workers) {
        const QString id = m_workers.key(w);
        if (w->resume())
            setState(id, TranscodeTask::State::Running);
    }
    launchNext();
}

void TaskQueue::cancelAll()
{
    // 先标记状态再取消 worker，避免 worker 的同步回调触发 launchNext 启动新任务
    for (TranscodeTask &t : m_tasks)
        if (t.state == TranscodeTask::State::Waiting
            || t.state == TranscodeTask::State::Running
            || t.state == TranscodeTask::State::Paused)
            setState(t.id, TranscodeTask::State::Cancelled);
    const auto workers = m_workers.values(); // cancel 可能同步修改 m_workers
    for (auto *w : workers)
        w->cancel();
}

void TaskQueue::clearAll()
{
    if (m_tasks.isEmpty())
        return;
    // 先清空列表：仍在退出的 worker 结束时找不到任务，不会再发 taskFinished
    m_tasks.clear();
    m_store.clear();
    m_queuePaused = false; // 队列恢复为可启动，避免"全部删除"后新任务无法自动开始
    emit tasksCleared();

    const auto workers = m_workers.values();
    for (auto *w : workers)
        w->cancel();
}

bool TaskQueue::loadFromStore(const QString &dbPath)
{
    if (m_store.isOpen())
        return true;
    if (!m_store.open(dbPath))
        return false;

    const QList<TranscodeTask> stored = m_store.loadAll();
    int loaded = 0;
    int requeued = 0;
    for (TranscodeTask t : stored) {
        if (t.id.isEmpty() || indexOf(t.id) >= 0)
            continue;
        if (t.state == TranscodeTask::State::Running
            || t.state == TranscodeTask::State::Paused) {
            // 上次退出时未完成：ffmpeg 无法断点续传，从头重新排队
            t.state = TranscodeTask::State::Waiting;
            t.progress = 0.0;
            m_store.upsert(t);
            ++requeued;
        }
        m_tasks.append(t);
        emit taskAdded(t);
        ++loaded;
    }

    if (loaded > 0) {
        if (requeued > 0)
            emit notice(tr("已加载 %1 个历史任务（%2 个中断任务已重新排队）")
                            .arg(loaded)
                            .arg(requeued));
        else
            emit notice(tr("已加载 %1 个历史任务").arg(loaded));
    }
    return true;
}

void TaskQueue::pauseTask(const QString &id)
{
    TranscodeTask *t = taskById(id);
    if (!t || t->state != TranscodeTask::State::Running)
        return;
    auto it = m_workers.find(id);
    if (it != m_workers.end() && (*it)->pause())
        setState(id, TranscodeTask::State::Paused);
    else
        emit notice(tr("该任务暂时无法暂停（进程尚未启动或当前平台不支持）"));
}

void TaskQueue::resumeTask(const QString &id)
{
    TranscodeTask *t = taskById(id);
    if (!t || t->state != TranscodeTask::State::Paused)
        return;
    auto it = m_workers.find(id);
    if (it != m_workers.end()) {
        if ((*it)->resume())
            setState(id, TranscodeTask::State::Running);
        else
            emit notice(tr("无法恢复该任务（进程可能已结束），请稍候或重新启动任务"));
    } else {
        // 等待阶段被暂停时没有 worker：回到等待队列，否则任务永远无法启动
        setState(id, TranscodeTask::State::Waiting);
        launchNext();
    }
}

void TaskQueue::cancelTask(const QString &id)
{
    TranscodeTask *t = taskById(id);
    if (!t)
        return;
    if (t->state == TranscodeTask::State::Done || t->state == TranscodeTask::State::Failed
        || t->state == TranscodeTask::State::Cancelled)
        return;
    setState(id, TranscodeTask::State::Cancelled); // 先标记，worker 的回报不会再覆盖
    auto it = m_workers.find(id);
    if (it != m_workers.end())
        (*it)->cancel();
}

void TaskQueue::startTask(const QString &id)
{
    TranscodeTask *t = taskById(id);
    if (!t || t->state != TranscodeTask::State::Waiting)
        return;
    if (m_queuePaused) {
        emit notice(tr("队列已暂停，请先点击“全部恢复”"));
        return;
    }
    if (runningCount() >= maxParallel()) {
        emit notice(tr("并发已满，任务将保持等待并在有空位时自动启动"));
        return;
    }
    launchWorker(t);
}

void TaskQueue::launchNext()
{
    if (m_queuePaused)
        return;
    while (runningCount() < maxParallel()) {
        TranscodeTask *t = nullptr;
        for (TranscodeTask &c : m_tasks) {
            if (c.state == TranscodeTask::State::Waiting) {
                t = &c;
                break;
            }
        }
        if (!t)
            break;
        launchWorker(t);
    }
}

void TaskQueue::launchWorker(TranscodeTask *t)
{
    TranscodeWorker *w = new TranscodeWorker(this);
    m_workers[t->id] = w;

    // 直连（队列作为 worker 的父对象）
    connect(w, &TranscodeWorker::done, this,
            [this, w](const QString &id, TranscodeTask::State st, QString error) {
                m_workers.remove(id);
                TranscodeTask *task = taskById(id);
                if (!task) {
                    // 任务已被"全部删除"移除：只回收 worker，不再通知 UI
                    w->deleteLater();
                    launchNext();
                    return;
                }
                // 取消优先：worker 无论回报 Done/Failed，都不能覆盖用户已取消的状态
                if (task->state == TranscodeTask::State::Cancelled
                    && st != TranscodeTask::State::Cancelled) {
                    st = TranscodeTask::State::Cancelled;
                    error.clear();
                }
                setState(id, st);
                emit taskFinished(id, st, error);
                w->deleteLater();
                launchNext(); // 完成后继续启动下一个等待任务
            });
    connect(w, &TranscodeWorker::progress, this,
            [this](const QString &id, double pct, qint64 elapsedMs, double speed) {
                TranscodeTask *t = taskById(id);
                if (!t)
                    return;
                const int oldBucket = static_cast<int>(t->progress / 10.0);
                t->progress = pct;
                if (static_cast<int>(pct / 10.0) != oldBucket)
                    m_store.upsert(*t); // 每约 10% 落库一次，异常退出后进度更接近真实值
                emit progress(id, pct, elapsedMs, speed);
            });

    // 按任务所选预设启动（优先使用入队时的快照）；名称为空表示"默认自动"
    TranscodePreset preset;
    if (!t->presetJson.isEmpty())
        preset = PresetManager::presetFromJson(t->presetJson);
    if (preset.name.isEmpty() && !t->presetName.isEmpty()) {
        preset = PresetManager::presetByName(t->presetName);
        if (preset.name.isEmpty())
            emit notice(tr("未找到预设「%1」，使用默认预设").arg(t->presetName));
    }
    if (preset.name.isEmpty())
        preset = PresetManager::defaultPreset();

    setState(t->id, TranscodeTask::State::Running);
    w->run(*t, preset);
}
