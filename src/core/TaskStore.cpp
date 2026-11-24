#include "core/TaskStore.hpp"

#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardPaths>

namespace {
int s_connectionCounter = 0;
}

TaskStore::~TaskStore()
{
    close();
}

bool TaskStore::open(const QString &dbPath)
{
    close();

    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        qWarning() << "任务数据库不可用：缺少 QSQLITE 驱动";
        return false;
    }

    const QString path = dbPath.isEmpty() ? defaultDatabasePath() : dbPath;
    if (path.isEmpty()) {
        qWarning() << "无法确定任务数据库路径";
        return false;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    m_connectionName = QStringLiteral("qencoder_tasks_%1").arg(++s_connectionCounter);
    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connectionName);
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        qWarning() << "打开任务数据库失败:" << path << m_db.lastError().text();
        close();
        return false;
    }

    {
        QSqlQuery pragma(m_db);
        pragma.exec(QStringLiteral("PRAGMA busy_timeout = 3000"));
    }

    if (!ensureSchema()) {
        close();
        return false;
    }
    return true;
}

void TaskStore::close()
{
    if (!m_db.isValid())
        return;
    const QString name = m_db.connectionName();
    m_db.close();
    m_db = QSqlDatabase();
    if (!name.isEmpty())
        QSqlDatabase::removeDatabase(name);
    m_connectionName.clear();
}

bool TaskStore::isOpen() const
{
    return m_db.isValid() && m_db.isOpen();
}

bool TaskStore::ensureSchema()
{
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral(
            "CREATE TABLE IF NOT EXISTS tasks ("
            "id TEXT PRIMARY KEY,"
            "input_path TEXT NOT NULL,"
            "output_path TEXT NOT NULL,"
            "preset_name TEXT,"
            "preset_json TEXT,"
            "state INTEGER NOT NULL,"
            "progress REAL NOT NULL DEFAULT 0,"
            "sort_order INTEGER NOT NULL"
            ")"))) {
        qWarning() << "创建任务表失败:" << q.lastError().text();
        return false;
    }

    // 旧版本数据库迁移：补充 preset_json 列（v1 -> v2）
    bool hasPresetJson = false;
    QSqlQuery info(m_db);
    if (info.exec(QStringLiteral("PRAGMA table_info(tasks)"))) {
        while (info.next())
            if (info.value(1).toString() == QLatin1String("preset_json"))
                hasPresetJson = true;
    }
    if (!hasPresetJson && !q.exec(QStringLiteral("ALTER TABLE tasks ADD COLUMN preset_json TEXT"))) {
        qWarning() << "迁移任务表失败:" << q.lastError().text();
        return false;
    }

    q.exec(QStringLiteral("PRAGMA user_version = 2"));
    return true;
}

QString TaskStore::defaultDatabasePath()
{
    const QString overridePath = qEnvironmentVariable("QENCODER_TASK_DB");
    if (!overridePath.isEmpty())
        return overridePath;

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (dir.isEmpty())
        return {};
    QDir().mkpath(dir);
    return QDir(dir).filePath(QStringLiteral("tasks.db"));
}

QList<TranscodeTask> TaskStore::loadAll() const
{
    QList<TranscodeTask> out;
    if (!isOpen())
        return out;

    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("SELECT id, input_path, output_path, preset_name, state, progress, "
                               "preset_json FROM tasks ORDER BY sort_order, id"))) {
        qWarning() << "读取任务失败:" << q.lastError().text();
        return out;
    }

    while (q.next()) {
        TranscodeTask t;
        t.id = q.value(0).toString();
        if (t.id.isEmpty())
            continue;
        t.inputPath = q.value(1).toString();
        t.outputPath = q.value(2).toString();
        t.presetName = q.value(3).toString();

        int state = q.value(4).toInt();
        if (state < static_cast<int>(TranscodeTask::State::Waiting)
            || state > static_cast<int>(TranscodeTask::State::Cancelled))
            state = static_cast<int>(TranscodeTask::State::Waiting);
        t.state = static_cast<TranscodeTask::State>(state);
        t.progress = qBound(0.0, q.value(5).toDouble(), 100.0);
        t.presetJson = q.value(6).toString();
        out.append(t);
    }
    return out;
}

void TaskStore::upsert(const TranscodeTask &task)
{
    if (!isOpen() || task.id.isEmpty())
        return;

    // 单条 UPSERT：新任务追加到队尾；已有任务只更新字段，不改变排队顺序
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral(
        "INSERT INTO tasks (id, input_path, output_path, preset_name, preset_json, state, progress, "
        "sort_order) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, (SELECT COALESCE(MAX(sort_order), 0) + 1 FROM tasks)) "
        "ON CONFLICT(id) DO UPDATE SET "
        "input_path = excluded.input_path, output_path = excluded.output_path, "
        "preset_name = excluded.preset_name, preset_json = excluded.preset_json, "
        "state = excluded.state, progress = excluded.progress"));
    q.addBindValue(task.id);
    q.addBindValue(task.inputPath);
    q.addBindValue(task.outputPath);
    q.addBindValue(task.presetName);
    q.addBindValue(task.presetJson);
    q.addBindValue(static_cast<int>(task.state));
    q.addBindValue(task.progress);
    if (!q.exec())
        qWarning() << "写入任务失败:" << q.lastError().text();
}

void TaskStore::remove(const QString &id)
{
    if (!isOpen() || id.isEmpty())
        return;
    QSqlQuery q(m_db);
    q.prepare(QStringLiteral("DELETE FROM tasks WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec())
        qWarning() << "删除任务失败:" << q.lastError().text();
}

void TaskStore::clear()
{
    if (!isOpen())
        return;
    QSqlQuery q(m_db);
    if (!q.exec(QStringLiteral("DELETE FROM tasks")))
        qWarning() << "清空任务失败:" << q.lastError().text();
}
