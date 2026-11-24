#pragma once

#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"
#include "core/TaskQueue.hpp"
#include "core/TranscodePreset.hpp"

#include <QDialog>
#include <QList>
#include <QSet>

namespace Ui {
class AddTasksDialog;
}

// 步骤 4：批量添加对话框
// - 多文件选择 + 添加目录（递归扫描媒体文件）
// - 任务预览列表；点"关闭（任务入队）"时才真正把任务加入队列
// - 选择预设（"默认自动"表示使用设置里的默认预设）
// - 自动命名输出路径：<输出目录>/<原文件名>_<预设名>.<容器扩展名>，自动避免覆盖/重名
class AddTasksDialog : public QDialog
{
    Q_OBJECT
public:
    explicit AddTasksDialog(TaskQueue *queue, QWidget *parent = nullptr);
    ~AddTasksDialog() override;

    // 供工具栏"添加目录"直接打开目录选择
    void promptForDirectory();
    bool hasPendingTasks() const { return !m_pending.isEmpty(); }

public slots:
    void accept() override; // 关闭时才入队

private slots:
    void onAddFiles();
    void onAddDir();

private:
    static QString makeOutputPath(const QString &inputPath, const TranscodePreset &preset,
                                  const QString &outDir);
    static int scanDir(const QString &dirPath, QStringList &outFiles);
    QString uniqueOutputPath(const QString &candidate);
    TranscodePreset currentPreset() const;
    QString currentPresetName() const;
    void updatePreviewState();

    Ui::AddTasksDialog *ui;

    TaskQueue *m_queue = nullptr;
    QList<TranscodeTask> m_pending;
    QSet<QString> m_plannedOutputs;
};
