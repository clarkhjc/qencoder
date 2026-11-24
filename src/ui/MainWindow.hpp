#pragma once

#include "core/TaskQueue.hpp"

#include <QMainWindow>

class QLabel;
class QProgressBar;
class QToolButton;

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
    void onStateChanged(const QString &id);
    void onProgress(const QString &id, double pct, qint64 elapsedMs, double speed);
    void onTaskFinished(const QString &id, TranscodeTask::State state, QString error);

private:
    void addTaskRow(const TranscodeTask &t);
    void updateRowButtons(int r);
    void updateEnvironmentStatus();
    void updateEmptyState();
    void updateToolbarState();
    void updateQueueSummary();
    QProgressBar *progressBarAt(int row) const;
    QLabel *progressLabelAt(int row) const;
    int rowOf(const QString &id) const;

    Ui::MainWindow *ui;
    QLabel *m_envLabel = nullptr;
    QLabel *m_summaryLabel = nullptr;
    QToolButton *m_startBtn = nullptr;
    QToolButton *m_pauseBtn = nullptr;
    QToolButton *m_resumeBtn = nullptr;
    QToolButton *m_cancelBtn = nullptr;
    QToolButton *m_clearBtn = nullptr;
    TaskQueue m_queue;
};
