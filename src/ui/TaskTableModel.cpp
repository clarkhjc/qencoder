#include "ui/TaskTableModel.hpp"

#include <QBrush>
#include <QColor>
#include <QFileInfo>

namespace {

QColor stateColor(TranscodeTask::State state)
{
    switch (state) {
    case TranscodeTask::State::Waiting: return QColor("#8b949e");
    case TranscodeTask::State::Running: return QColor("#2ea043");
    case TranscodeTask::State::Paused: return QColor("#d29922");
    case TranscodeTask::State::Done: return QColor("#2ea043");
    case TranscodeTask::State::Failed: return QColor("#f85149");
    case TranscodeTask::State::Cancelled: return QColor("#8b949e");
    }
    return {};
}

} // namespace

TaskTableModel::TaskTableModel(QObject *parent) : QAbstractTableModel(parent) {}

int TaskTableModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : m_rows.size();
}

int TaskTableModel::columnCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : ColumnCount;
}

QVariant TaskTableModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()
        || index.column() < 0 || index.column() >= ColumnCount)
        return {};

    const Row &row = m_rows.at(index.row());
    const TranscodeTask &task = row.task;

    // 供委托与外部代码直接读取的原始数据
    switch (role) {
    case TaskIdRole: return task.id;
    case StateRole: return QVariant::fromValue(task.state);
    case ProgressRole: return task.progress;
    case ElapsedMsRole: return row.elapsedMs;
    case SpeedRole: return row.speed;
    case ErrorRole: return row.error;
    default: break;
    }

    switch (role) {
    case Qt::DisplayRole:
        switch (index.column()) {
        case StateColumn:
            return QStringLiteral("● ") + stateText(task.state);
        case TaskColumn:
            return row.fileName + (row.sourceMissing ? tr("（源文件缺失）") : QString());
        case ProgressColumn:
            return QStringLiteral("%1%").arg(
                static_cast<int>(qBound(0.0, task.progress, 100.0)));
        case TimeSpeedColumn:
            return timeSpeedText(row);
        case OutputColumn:
            return task.outputPath;
        case ActionColumn:
            return {};
        }
        break;
    case Qt::ForegroundRole:
        if (index.column() == StateColumn)
            return QBrush(stateColor(task.state));
        break;
    case Qt::ToolTipRole:
        switch (index.column()) {
        case StateColumn:
            return row.error.isEmpty() ? QVariant() : QVariant(row.error.simplified());
        case TaskColumn:
            return row.sourceMissing ? tr("源文件不存在：%1").arg(task.inputPath)
                                     : task.inputPath;
        case ProgressColumn:
            return tr("转码进度");
        case TimeSpeedColumn:
            return tr("已用时 / 转码速度");
        case OutputColumn:
            if (task.state == TranscodeTask::State::Failed && !row.error.isEmpty())
                return row.error;
            return task.outputPath;
        case ActionColumn:
            return {};
        }
        break;
    case Qt::TextAlignmentRole:
        return static_cast<int>(Qt::AlignLeft | Qt::AlignVCenter);
    default:
        break;
    }
    return {};
}

QVariant TaskTableModel::headerData(int section, Qt::Orientation orientation, int role) const
{
    if (orientation != Qt::Horizontal || role != Qt::DisplayRole)
        return QAbstractTableModel::headerData(section, orientation, role);

    switch (section) {
    case StateColumn: return tr("状态");
    case TaskColumn: return tr("任务");
    case ProgressColumn: return tr("进度");
    case TimeSpeedColumn: return tr("耗时/速度");
    case OutputColumn: return tr("输出路径");
    case ActionColumn: return tr("操作");
    default: return {};
    }
}

Qt::ItemFlags TaskTableModel::flags(const QModelIndex &index) const
{
    if (!index.isValid())
        return Qt::NoItemFlags;
    return QAbstractTableModel::flags(index) & ~Qt::ItemIsEditable;
}

void TaskTableModel::addTask(TranscodeTask task)
{
    Row row;
    row.task = task;
    row.fileName = QFileInfo(task.inputPath).fileName();
    if (row.fileName.isEmpty())
        row.fileName = task.inputPath;
    row.sourceMissing = !QFileInfo::exists(task.inputPath);

    const int existing = rowOf(task.id);
    if (existing >= 0) {
        // 正常情况下队列里的 id 唯一；若重复则覆盖，避免映射错乱
        m_rows[existing] = row;
        emit dataChanged(index(existing, 0), index(existing, ColumnCount - 1));
        return;
    }

    const int rowIndex = m_rows.size();
    beginInsertRows(QModelIndex(), rowIndex, rowIndex);
    m_rows.append(row);
    m_rowById.insert(task.id, rowIndex);
    endInsertRows();
}

void TaskTableModel::updateState(const QString &id, TranscodeTask::State state)
{
    const int rowIndex = rowOf(id);
    if (rowIndex < 0)
        return;
    Row &row = m_rows[rowIndex];
    if (row.task.state == state)
        return;

    row.task.state = state;
    if (state == TranscodeTask::State::Done)
        row.task.progress = 100.0;
    emit dataChanged(index(rowIndex, 0), index(rowIndex, ColumnCount - 1));
}

void TaskTableModel::updateProgress(const QString &id, double pct, qint64 elapsedMs, double speed)
{
    const int rowIndex = rowOf(id);
    if (rowIndex < 0)
        return;
    Row &row = m_rows[rowIndex];
    row.task.progress = qBound(0.0, pct, 100.0);
    row.elapsedMs = qMax<qint64>(0, elapsedMs);
    row.speed = qMax(0.0, speed);
    emit dataChanged(index(rowIndex, ProgressColumn), index(rowIndex, TimeSpeedColumn));
}

void TaskTableModel::setError(const QString &id, const QString &error)
{
    const int rowIndex = rowOf(id);
    if (rowIndex < 0)
        return;
    Row &row = m_rows[rowIndex];
    if (row.error == error)
        return;
    row.error = error;
    emit dataChanged(index(rowIndex, 0), index(rowIndex, OutputColumn));
}

void TaskTableModel::removeTask(const QString &id)
{
    const int rowIndex = rowOf(id);
    if (rowIndex < 0)
        return;

    beginRemoveRows(QModelIndex(), rowIndex, rowIndex);
    m_rows.removeAt(rowIndex);
    m_rowById.remove(id);
    // 被删除行之后的所有行号都向前移动一位
    for (int i = rowIndex; i < m_rows.size(); ++i)
        m_rowById.insert(m_rows.at(i).task.id, i);
    endRemoveRows();
}

void TaskTableModel::clear()
{
    if (m_rows.isEmpty())
        return;
    beginResetModel();
    m_rows.clear();
    m_rowById.clear();
    endResetModel();
}

int TaskTableModel::rowOf(const QString &id) const
{
    const auto it = m_rowById.constFind(id);
    if (it == m_rowById.constEnd())
        return -1;
    const int rowIndex = it.value();
    if (rowIndex < 0 || rowIndex >= m_rows.size() || m_rows.at(rowIndex).task.id != id)
        return -1;
    return rowIndex;
}

QString TaskTableModel::stateText(TranscodeTask::State state) const
{
    switch (state) {
    case TranscodeTask::State::Waiting: return tr("等待");
    case TranscodeTask::State::Running: return tr("转码中");
    case TranscodeTask::State::Paused: return tr("暂停");
    case TranscodeTask::State::Done: return tr("完成");
    case TranscodeTask::State::Failed: return tr("失败");
    case TranscodeTask::State::Cancelled: return tr("已取消");
    }
    return {};
}

QString TaskTableModel::timeSpeedText(const Row &row) const
{
    if (row.elapsedMs <= 0 && row.speed <= 0.001)
        return QStringLiteral("—");

    const qint64 secs = qMax<qint64>(0, row.elapsedMs / 1000);
    QString text = QStringLiteral("%1:%2")
                       .arg(secs / 60, 2, 10, QChar('0'))
                       .arg(secs % 60, 2, 10, QChar('0'));
    if (row.speed > 0.001)
        text += QStringLiteral(" · %1x").arg(row.speed, 0, 'f', 2);
    return text;
}
