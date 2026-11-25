#pragma once

#include "core/TaskQueue.hpp"

#include <QMainWindow>

class QLabel;
class QToolButton;
class TaskTableModel;

namespace Ui {
class MainWindow;
}

// 步骤 6 + 补充：主窗——任务表 + 工具栏 + 队列控制 + 设置
class MainWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    // 供程序化添加任务（命令行/测试/截图预览）
    TaskQueue &taskQueue() { return m_queue; }

private slots:
    void onAddFiles();
    void onAddDir();
    void onOpenPresets();
    void onSettings();
    void onStart();
    void onPauseQueue();
    void onResumeQueue();
    void onCancelAll();
    void onRemoveAll();
    void onTaskAdded(TranscodeTask t);
    void onTaskFinished(const QString &id, TranscodeTask::State state, QString error);
    void onTaskAction(const QString &id, int action);

private:
    void updateEnvironmentStatus();
    void updateEmptyState();
    void updateToolbarState();
    void updateQueueSummary();

    Ui::MainWindow *ui;
    QLabel *m_envLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QToolButton *m_startBtn = nullptr;
    QToolButton *m_pauseBtn = nullptr;
    QToolButton *m_resumeBtn = nullptr;
    QToolButton *m_cancelBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    TaskTableModel *m_model = nullptr;
    TaskQueue m_queue;
};
