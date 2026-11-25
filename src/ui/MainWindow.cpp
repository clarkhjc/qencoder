#include "ui/MainWindow.hpp"
#include "ui_MainWindow.h"
#include "ui/AddTasksDialog.hpp"
#include "core/EnvironmentCheck.hpp"
#include "ui/IconFactory.hpp"
#include "ui/PresetDialog.hpp"
#include "settings/Settings.hpp"
#include "ui/SettingsDialog.hpp"
#include "ui/TaskTableDelegates.hpp"
#include "ui/TaskTableModel.hpp"

#include <QApplication>
#include <QFileInfo>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QTableView>
#include <QToolBar>
#include <QToolButton>

MainWindow::MainWindow(QWidget *parent) : QMainWindow(parent), ui(new Ui::MainWindow)
{
    ui->setupUi(this);
    setWindowIcon(IconFactory::icon("play"));

    // ---- 工具栏（布局来自 MainWindow.ui，按钮仍用彩色图标动态创建） ----
    QToolBar *toolbar = ui->mainToolBar;
    toolbar->setMovable(false);
    toolbar->setFloatable(false);
    toolbar->setIconSize(QSize(18, 18));
    toolbar->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);

    auto makeToolButton = [](const QString &text, const QString &icon) {
        auto *b = new QToolButton;
        b->setText(text);
        b->setToolTip(text);
        b->setIcon(IconFactory::icon(icon));
        b->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    auto *addFiles = makeToolButton(tr("添加文件"), "add-file");
    auto *addDir = makeToolButton(tr("添加目录"), "add-folder");
    m_startBtn = makeToolButton(tr("启动队列"), "play");
    m_pauseBtn = makeToolButton(tr("全部暂停"), "pause");
    m_resumeBtn = makeToolButton(tr("全部恢复"), "resume");
    m_cancelBtn = makeToolButton(tr("全部取消"), "cancel");
    m_clearBtn = makeToolButton(tr("全部删除"), "trash");
    auto *presets = makeToolButton(tr("参数预设"), "sliders");
    auto *settings = makeToolButton(tr("设置"), "settings");

    toolbar->addWidget(addFiles);
    toolbar->addWidget(addDir);
    toolbar->addSeparator();
    toolbar->addWidget(m_startBtn);
    toolbar->addWidget(m_pauseBtn);
    toolbar->addWidget(m_resumeBtn);
    toolbar->addWidget(m_cancelBtn);
    toolbar->addWidget(m_clearBtn);
    toolbar->addSeparator();
    toolbar->addWidget(presets);
    toolbar->addWidget(settings);

    // ---- 任务表 ----
    auto *table = ui->taskTable;
    m_model = new TaskTableModel(this);
    table->setModel(m_model);

    auto *actionDelegate = new ActionDelegate(table);
    table->setItemDelegateForColumn(TaskTableModel::ProgressColumn,
                                    new ProgressDelegate(table));
    table->setItemDelegateForColumn(TaskTableModel::TimeSpeedColumn,
                                    new TimeSpeedDelegate(table));
    table->setItemDelegateForColumn(TaskTableModel::ActionColumn, actionDelegate);

    auto *header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setMinimumSectionSize(64);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(TaskTableModel::StateColumn, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(TaskTableModel::TaskColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(TaskTableModel::ProgressColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(TaskTableModel::TimeSpeedColumn, QHeaderView::Fixed);
    header->setSectionResizeMode(TaskTableModel::OutputColumn, QHeaderView::Stretch);
    header->setSectionResizeMode(TaskTableModel::ActionColumn, QHeaderView::Fixed);
    table->setColumnWidth(TaskTableModel::ProgressColumn, 180);
    table->setColumnWidth(TaskTableModel::TimeSpeedColumn, 120);
    table->setColumnWidth(TaskTableModel::ActionColumn, 190);

    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);
    table->verticalHeader()->setSectionResizeMode(QHeaderView::Fixed);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setTextElideMode(Qt::ElideMiddle);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setWordWrap(false);
    table->setMouseTracking(true);

    // ---- 空状态页 ----
    ui->emptyIconLabel->setPixmap(IconFactory::icon("add-file").pixmap(72, 72));
    ui->emptyAddFileBtn->setProperty("accent", true);
    ui->emptyAddFileBtn->setCursor(Qt::PointingHandCursor);
    ui->emptyAddDirBtn->setCursor(Qt::PointingHandCursor);

    // ---- 连接 ----
    connect(addFiles, &QToolButton::clicked, this, &MainWindow::onAddFiles);
    connect(addDir, &QToolButton::clicked, this, &MainWindow::onAddDir);
    connect(presets, &QToolButton::clicked, this, &MainWindow::onOpenPresets);
    connect(settings, &QToolButton::clicked, this, &MainWindow::onSettings);
    connect(m_startBtn, &QToolButton::clicked, this, &MainWindow::onStart);
    connect(m_pauseBtn, &QToolButton::clicked, this, &MainWindow::onPauseQueue);
    connect(m_resumeBtn, &QToolButton::clicked, this, &MainWindow::onResumeQueue);
    connect(m_cancelBtn, &QToolButton::clicked, this, &MainWindow::onCancelAll);
    connect(m_clearBtn, &QToolButton::clicked, this, &MainWindow::onRemoveAll);
    connect(ui->emptyAddFileBtn, &QPushButton::clicked, this, &MainWindow::onAddFiles);
    connect(ui->emptyAddDirBtn, &QPushButton::clicked, this, &MainWindow::onAddDir);

    connect(&m_queue, &TaskQueue::taskAdded, this, &MainWindow::onTaskAdded);
    connect(&m_queue, &TaskQueue::stateChanged, m_model, &TaskTableModel::updateState);
    connect(&m_queue, &TaskQueue::stateChanged, this,
            [this](const QString &, TranscodeTask::State) { updateToolbarState(); });
    connect(&m_queue, &TaskQueue::progress, m_model, &TaskTableModel::updateProgress);
    connect(&m_queue, &TaskQueue::taskFinished, this, &MainWindow::onTaskFinished);
    connect(actionDelegate, &ActionDelegate::actionTriggered, this,
            &MainWindow::onTaskAction);
    connect(&m_queue, &TaskQueue::notice, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 5000); });
    connect(&m_queue, &TaskQueue::tasksCleared, this, [this] {
        m_model->clear();
        updateEmptyState();
        updateToolbarState();
    });

    // ---- 状态栏 ----
    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName("queueSummary");
    statusBar()->addWidget(m_summaryLabel);
    m_envLabel = new QLabel(this);
    m_envLabel->setObjectName("envStatus");
    statusBar()->addPermanentWidget(m_envLabel);
    statusBar()->setSizeGripEnabled(false);

    m_queue.setMaxParallel(Settings::maxParallel());
    updateEnvironmentStatus();
    updateEmptyState();
    updateToolbarState();

    // 加载上次会话的任务（数据库不可用时仍可在内存中正常使用）
    if (!m_queue.loadFromStore())
        statusBar()->showMessage(tr("无法打开任务数据库，任务状态将不会保存"), 8000);
}

MainWindow::~MainWindow()
{
    delete ui;
}

void MainWindow::updateEnvironmentStatus()
{
    const bool ok = EnvironmentCheck::isValid();
    if (ok) {
        m_envLabel->setText(tr("● ffmpeg v%1").arg(EnvironmentCheck::ffmpegVersion()));
        m_envLabel->setToolTip(EnvironmentCheck::ffmpegPath());
    } else {
        m_envLabel->setText(tr("● 未找到 ffmpeg/ffprobe"));
        m_envLabel->setToolTip(tr("请在“设置 → ffmpeg 目录”中指定"));
    }
    m_envLabel->setProperty("state", ok ? QStringLiteral("ok") : QStringLiteral("missing"));
    m_envLabel->style()->unpolish(m_envLabel);
    m_envLabel->style()->polish(m_envLabel);
}

void MainWindow::updateEmptyState()
{
    ui->mainStack->setCurrentWidget(m_model->rowCount() > 0
                                        ? static_cast<QWidget *>(ui->taskPage)
                                        : ui->emptyPage);
}

void MainWindow::updateToolbarState()
{
    bool hasWaiting = false;
    bool hasRunning = false;
    bool hasPaused = false;
    bool hasActive = false;
    for (const TranscodeTask &t : m_queue.tasks()) {
        switch (t.state) {
        case TranscodeTask::State::Waiting:
            hasWaiting = true;
            hasActive = true;
            break;
        case TranscodeTask::State::Running:
            hasRunning = true;
            hasActive = true;
            break;
        case TranscodeTask::State::Paused:
            hasPaused = true;
            hasActive = true;
            break;
        case TranscodeTask::State::Done:
        case TranscodeTask::State::Failed:
        case TranscodeTask::State::Cancelled:
            break;
        }
    }
    m_startBtn->setEnabled(hasWaiting);
    m_pauseBtn->setEnabled(hasRunning);
    m_resumeBtn->setEnabled(hasPaused);
    m_cancelBtn->setEnabled(hasActive);
    m_clearBtn->setEnabled(!m_queue.tasks().isEmpty());

    updateQueueSummary();
}

void MainWindow::updateQueueSummary()
{
    int total = 0;
    int waiting = 0;
    int running = 0;
    int paused = 0;
    int ended = 0;
    for (const TranscodeTask &t : m_queue.tasks()) {
        ++total;
        switch (t.state) {
        case TranscodeTask::State::Waiting: ++waiting; break;
        case TranscodeTask::State::Running: ++running; break;
        case TranscodeTask::State::Paused: ++paused; break;
        case TranscodeTask::State::Done:
        case TranscodeTask::State::Failed:
        case TranscodeTask::State::Cancelled: ++ended; break;
        }
    }
    if (total == 0)
        m_summaryLabel->setText(tr("就绪"));
    else
        m_summaryLabel->setText(
            tr("共 %1 个任务 · 等待 %2 · 进行中 %3 · 已结束 %4")
                .arg(total)
                .arg(waiting)
                .arg(running + paused)
                .arg(ended));
}

void MainWindow::onAddFiles()
{
    // 用户场景：添加文件后关闭对话框即入队，不自动启动任务，等用户点"启动队列"
    m_queue.setAutoStart(false);
    AddTasksDialog dlg(&m_queue, this);
    dlg.exec();
    m_queue.setAutoStart(true);
}

void MainWindow::onAddDir()
{
    m_queue.setAutoStart(false);
    AddTasksDialog dlg(&m_queue, this);
    dlg.promptForDirectory();
    if (dlg.hasPendingTasks())
        dlg.exec();
    m_queue.setAutoStart(true);
}

void MainWindow::onOpenPresets()
{
    PresetDialog dlg(this);
    dlg.exec();
}

void MainWindow::onSettings()
{
    SettingsDialog dlg(this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_queue.setMaxParallel(Settings::maxParallel());
    // 用户可能修改了 ffmpeg 目录，重新探测
    EnvironmentCheck::initialize();
    updateEnvironmentStatus();
}

void MainWindow::onStart()
{
    m_queue.start();
}

void MainWindow::onPauseQueue()
{
    m_queue.pauseAll();
}

void MainWindow::onResumeQueue()
{
    m_queue.resumeAll();
}

void MainWindow::onCancelAll()
{
    m_queue.cancelAll();
}

void MainWindow::onRemoveAll()
{
    const int total = m_queue.tasks().size();
    if (total == 0)
        return;

    int active = 0;
    for (const TranscodeTask &t : m_queue.tasks())
        if (t.state == TranscodeTask::State::Running || t.state == TranscodeTask::State::Paused)
            ++active;

    const QString question =
        active > 0
            ? tr("当前有 %1 个任务正在运行或暂停，全部删除会先取消它们。\n确定删除全部 %2 个任务？")
                  .arg(active)
                  .arg(total)
            : tr("确定删除全部 %1 个任务？").arg(total);
    if (QMessageBox::question(this, tr("全部删除"), question,
                              QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
        != QMessageBox::Yes)
        return;

    m_queue.clearAll();
    statusBar()->showMessage(tr("已删除全部任务"), 4000);
}

void MainWindow::onTaskAdded(TranscodeTask t)
{
    m_model->addTask(t);
    updateEmptyState();
    updateToolbarState();
}

void MainWindow::onTaskAction(const QString &id, int action)
{
    switch (action) {
    case ActionDelegate::Start:
        m_queue.startTask(id);
        break;
    case ActionDelegate::Pause:
        m_queue.pauseTask(id);
        break;
    case ActionDelegate::Resume:
        m_queue.resumeTask(id);
        break;
    case ActionDelegate::Stop:
        m_queue.cancelTask(id);
        break;
    case ActionDelegate::Remove: {
        // 等待中的任务先取消再移除；已结束任务 cancelTask 是空操作
        m_queue.cancelTask(id);
        const int idx = m_queue.indexOf(id);
        if (idx < 0 || !m_queue.removeTask(idx))
            return;
        m_model->removeTask(id);
        updateEmptyState();
        break;
    }
    default:
        return;
    }
    updateToolbarState();
}

void MainWindow::onTaskFinished(const QString &id, TranscodeTask::State state, QString error)
{
    m_model->setError(id, error);

    // 系统通知（闪窗），按设置开关；取消不打扰用户
    if (Settings::systemNotifications()
        && (state == TranscodeTask::State::Done || state == TranscodeTask::State::Failed))
        QApplication::alert(this);

    QString name = id;
    const int idx = m_queue.indexOf(id);
    if (idx >= 0)
        name = QFileInfo(m_queue.tasks().at(idx).inputPath).fileName();

    if (state == TranscodeTask::State::Failed && !error.isEmpty())
        statusBar()->showMessage(
            tr("任务 %1 失败：%2").arg(name, error.simplified().left(200)), 8000);
    else
        statusBar()->showMessage(tr("任务 %1 已结束").arg(name), 5000);
}
