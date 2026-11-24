#include "ui/MainWindow.hpp"
#include "ui_MainWindow.h"
#include "ui/AddTasksDialog.hpp"
#include "core/EnvironmentCheck.hpp"
#include "ui/IconFactory.hpp"
#include "ui/PresetDialog.hpp"
#include "settings/Settings.hpp"
#include "ui/SettingsDialog.hpp"

#include <QApplication>
#include <QColor>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QStatusBar>
#include <QStyle>
#include <QToolBar>
#include <QToolButton>

namespace {

QString stateText(TranscodeTask::State s)
{
    switch (s) {
    case TranscodeTask::State::Waiting: return QObject::tr("等待");
    case TranscodeTask::State::Running: return QObject::tr("转码中");
    case TranscodeTask::State::Paused: return QObject::tr("暂停");
    case TranscodeTask::State::Done: return QObject::tr("完成");
    case TranscodeTask::State::Failed: return QObject::tr("失败");
    case TranscodeTask::State::Cancelled: return QObject::tr("已取消");
    }
    return {};
}

QString stateLabel(TranscodeTask::State s)
{
    return QStringLiteral("●  ") + stateText(s);
}

QColor stateColor(TranscodeTask::State s)
{
    switch (s) {
    case TranscodeTask::State::Waiting: return QColor("#8b949e");
    case TranscodeTask::State::Running: return QColor("#2ea043");
    case TranscodeTask::State::Paused: return QColor("#d29922");
    case TranscodeTask::State::Done: return QColor("#2ea043");
    case TranscodeTask::State::Failed: return QColor("#f85149");
    case TranscodeTask::State::Cancelled: return QColor("#8b949e");
    }
    return {};
}

bool isTerminal(TranscodeTask::State s)
{
    return s == TranscodeTask::State::Done || s == TranscodeTask::State::Failed
           || s == TranscodeTask::State::Cancelled;
}

} // namespace

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
    QTableWidget *table = ui->taskTable;
    table->setColumnCount(6);
    table->setHorizontalHeaderLabels(
        QStringList() << tr("状态") << tr("任务") << tr("进度") << tr("耗时/速度")
                      << tr("输出路径") << tr("操作"));
    auto *header = table->horizontalHeader();
    header->setStretchLastSection(false);
    header->setHighlightSections(false);
    header->setMinimumSectionSize(64);
    header->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    header->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    header->setSectionResizeMode(1, QHeaderView::Stretch);
    header->setSectionResizeMode(2, QHeaderView::Fixed);
    header->setSectionResizeMode(3, QHeaderView::Fixed);
    header->setSectionResizeMode(4, QHeaderView::Stretch);
    header->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    table->setColumnWidth(2, 180);
    table->setColumnWidth(3, 120);
    table->verticalHeader()->setVisible(false);
    table->verticalHeader()->setDefaultSectionSize(46);
    table->setShowGrid(false);
    table->setAlternatingRowColors(true);
    table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    table->setSelectionBehavior(QAbstractItemView::SelectRows);
    table->setSelectionMode(QAbstractItemView::SingleSelection);
    table->setTextElideMode(Qt::ElideMiddle);
    table->setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    table->setWordWrap(false);

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
    connect(&m_queue, &TaskQueue::stateChanged, this, &MainWindow::onStateChanged);
    connect(&m_queue, &TaskQueue::progress, this, &MainWindow::onProgress);
    connect(&m_queue, &TaskQueue::taskFinished, this, &MainWindow::onTaskFinished);
    connect(&m_queue, &TaskQueue::notice, this,
            [this](const QString &msg) { statusBar()->showMessage(msg, 5000); });
    connect(&m_queue, &TaskQueue::tasksCleared, this, [this] {
        ui->taskTable->setRowCount(0);
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
    ui->mainStack->setCurrentWidget(ui->taskTable->rowCount() > 0
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

int MainWindow::rowOf(const QString &id) const
{
    for (int i = 0; i < ui->taskTable->rowCount(); ++i)
        if (ui->taskTable->item(i, 0)
            && ui->taskTable->item(i, 0)->data(Qt::UserRole).toString() == id)
            return i;
    return -1;
}

QProgressBar *MainWindow::progressBarAt(int row) const
{
    if (QWidget *w = ui->taskTable->cellWidget(row, 2))
        return w->findChild<QProgressBar *>();
    return nullptr;
}

QLabel *MainWindow::progressLabelAt(int row) const
{
    if (QWidget *w = ui->taskTable->cellWidget(row, 2))
        return w->findChild<QLabel *>(QStringLiteral("progressValue"));
    return nullptr;
}

void MainWindow::addTaskRow(const TranscodeTask &t)
{
    QTableWidget *table = ui->taskTable;
    const int r = table->rowCount();
    table->insertRow(r);

    auto *stateCell = new QTableWidgetItem(stateLabel(t.state));
    stateCell->setData(Qt::UserRole, t.id);
    stateCell->setForeground(QBrush(stateColor(t.state)));
    stateCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setItem(r, 0, stateCell);

    auto *nameCell = new QTableWidgetItem(QFileInfo(t.inputPath).fileName());
    if (QFileInfo::exists(t.inputPath)) {
        nameCell->setToolTip(t.inputPath);
    } else {
        nameCell->setText(nameCell->text() + tr("（源文件缺失）"));
        nameCell->setToolTip(tr("源文件不存在：") + t.inputPath);
    }
    nameCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setItem(r, 1, nameCell);

    auto *bar = new QProgressBar;
    bar->setRange(0, 100);
    bar->setValue(static_cast<int>(t.progress));
    bar->setTextVisible(false);
    bar->setFixedHeight(8);
    auto *pct = new QLabel(QStringLiteral("%1%").arg(static_cast<int>(t.progress)));
    pct->setObjectName("progressValue");
    pct->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    pct->setFixedWidth(40);
    auto *barWrap = new QWidget;
    auto *barLayout = new QHBoxLayout(barWrap);
    barLayout->setContentsMargins(10, 0, 10, 0);
    barLayout->setSpacing(8);
    barLayout->addWidget(bar, 1, Qt::AlignVCenter);
    barLayout->addWidget(pct, 0, Qt::AlignVCenter);
    table->setCellWidget(r, 2, barWrap);

    auto *timeCell = new QTableWidgetItem(QStringLiteral("—"));
    timeCell->setToolTip(tr("已用时 / 转码速度"));
    timeCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setItem(r, 3, timeCell);

    auto *outCell = new QTableWidgetItem(t.outputPath);
    outCell->setToolTip(t.outputPath);
    outCell->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    table->setItem(r, 4, outCell);

    auto *ops = new QWidget;
    ops->setObjectName("rowActions");
    auto *opsLayout = new QHBoxLayout(ops);
    opsLayout->setContentsMargins(6, 9, 6, 9);
    opsLayout->setSpacing(2);

    auto makeAction = [](const QString &name, const QString &icon, const QString &tip) {
        auto *b = new QToolButton;
        b->setObjectName(name);
        b->setIcon(IconFactory::icon(icon));
        b->setIconSize(QSize(16, 16));
        b->setText(tip);
        b->setToolTip(tip);
        b->setAccessibleName(tip);
        b->setFixedSize(28, 28);
        b->setCursor(Qt::PointingHandCursor);
        return b;
    };

    auto *start = makeAction("btnStart", "play", tr("启动"));
    auto *pause = makeAction("btnPause", "pause", tr("暂停"));
    auto *resume = makeAction("btnResume", "resume", tr("恢复"));
    auto *cancel = makeAction("btnCancel", "stop", tr("停止"));
    auto *remove = makeAction("btnRemove", "trash", tr("移除"));

    ops->setProperty("taskId", t.id);
    opsLayout->addWidget(start);
    opsLayout->addWidget(pause);
    opsLayout->addWidget(resume);
    opsLayout->addWidget(cancel);
    opsLayout->addWidget(remove);
    opsLayout->addStretch();
    table->setCellWidget(r, 5, ops);

    connect(start, &QToolButton::clicked, this, [this, id = t.id] {
        m_queue.startTask(id);
        onStateChanged(id);
    });
    connect(pause, &QToolButton::clicked, this, [this, id = t.id] {
        m_queue.pauseTask(id);
        onStateChanged(id);
    });
    connect(resume, &QToolButton::clicked, this, [this, id = t.id] {
        m_queue.resumeTask(id);
        onStateChanged(id);
    });
    connect(cancel, &QToolButton::clicked, this, [this, id = t.id] {
        m_queue.cancelTask(id);
        onStateChanged(id);
    });
    connect(remove, &QToolButton::clicked, this, [this, id = t.id] {
        const int idx = m_queue.indexOf(id);
        if (idx < 0 || !m_queue.removeTask(idx))
            return;
        const int row = rowOf(id);
        if (row >= 0)
            ui->taskTable->removeRow(row);
        updateEmptyState();
        updateToolbarState();
    });
    updateRowButtons(r);
}

void MainWindow::updateRowButtons(int r)
{
    auto *w = ui->taskTable->cellWidget(r, 5);
    if (!w)
        return;
    const TranscodeTask *t = nullptr;
    for (const TranscodeTask &ct : m_queue.tasks())
        if (ct.id == w->property("taskId").toString())
            t = &ct;
    if (!t)
        return;
    using S = TranscodeTask::State;
    for (QToolButton *b : w->findChildren<QToolButton *>()) {
        if (b->objectName() == "btnStart")
            b->setVisible(t->state == S::Waiting);
        else if (b->objectName() == "btnPause")
            b->setVisible(t->state == S::Running);
        else if (b->objectName() == "btnResume")
            b->setVisible(t->state == S::Paused);
        else if (b->objectName() == "btnCancel")
            b->setVisible(t->state != S::Done && t->state != S::Failed
                          && t->state != S::Cancelled);
        else if (b->objectName() == "btnRemove")
            b->setVisible(!m_queue.isActive(t->id)
                          && (t->state == S::Waiting || isTerminal(t->state)));
    }
}

void MainWindow::onTaskAdded(TranscodeTask t)
{
    addTaskRow(t);
    updateEmptyState();
    updateToolbarState();
}

void MainWindow::onStateChanged(const QString &id)
{
    int r = rowOf(id);
    if (r < 0)
        return;
    const TranscodeTask *t = nullptr;
    for (const TranscodeTask &ct : m_queue.tasks())
        if (ct.id == id)
            t = &ct;
    if (!t)
        return;

    auto *cell = ui->taskTable->item(r, 0);
    if (cell) {
        cell->setText(stateLabel(t->state));
        cell->setForeground(QBrush(stateColor(t->state)));
    }
    updateRowButtons(r);
    updateToolbarState();
}

void MainWindow::onProgress(const QString &id, double pct, qint64 elapsedMs, double speed)
{
    const int r = rowOf(id);
    if (r < 0)
        return;
    const int value = qBound(0, static_cast<int>(pct), 100);
    if (auto *bar = progressBarAt(r))
        bar->setValue(value);
    if (auto *label = progressLabelAt(r))
        label->setText(QStringLiteral("%1%").arg(value));
    if (auto *cell = ui->taskTable->item(r, 3)) {
        const qint64 secs = elapsedMs / 1000;
        QString text = QStringLiteral("%1:%2")
                           .arg(secs / 60, 2, 10, QChar('0'))
                           .arg(secs % 60, 2, 10, QChar('0'));
        if (speed > 0.001)
            text += QStringLiteral(" · %1x").arg(speed, 0, 'f', 2);
        cell->setText(text);
    }
}

void MainWindow::onTaskFinished(const QString &id, TranscodeTask::State state, QString error)
{
    onStateChanged(id);
    const int r = rowOf(id);
    if (r >= 0) {
        if (auto *bar = progressBarAt(r)) {
            // 只有真正完成才填满进度条，失败/取消保留实际进度
            if (state == TranscodeTask::State::Done) {
                bar->setValue(bar->maximum());
                if (auto *label = progressLabelAt(r))
                    label->setText(QStringLiteral("100%"));
            }
        }
        if (auto *stateCell = ui->taskTable->item(r, 0))
            stateCell->setToolTip(error.isEmpty() ? QString() : error.simplified());
        if (state == TranscodeTask::State::Failed && !error.isEmpty())
            if (auto *outCell = ui->taskTable->item(r, 4))
                outCell->setToolTip(error);
    }

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
