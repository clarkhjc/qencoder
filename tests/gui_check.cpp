// GUI 环境检查：验证主窗工具栏按钮的图标与文字，并保存截图便于目视确认。
// - QENCODER_GUI_CHECK_THEME=dark|light|pink 选择主题（默认 dark）
// - QENCODER_GUI_CHECK_DEMO=1 时填充示例任务并截取各对话框，便于 UI 走查
#include "ui/AddTasksDialog.hpp"
#include "core/EnvironmentCheck.hpp"
#include "ui/MainWindow.hpp"
#include "ui/PresetDialog.hpp"
#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"
#include "ui/SettingsDialog.hpp"
#include "settings/ThemeManager.hpp"

#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QSettings>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QToolBar>
#include <QToolButton>

static QString g_theme;

static void saveShot(QWidget *w, const QString &name)
{
    w->show();
    QApplication::processEvents();
    const QString path =
        QString("%1/qencoder_%2_%3.png").arg(QDir::tempPath(), name, g_theme);
    if (!w->grab().save(path))
        qDebug() << "screenshot save failed:" << path;
    else
        qDebug() << "screenshot:" << path;
    w->hide();
}

static TranscodeTask makeDemoTask(const QString &fileName, TranscodeTask::State state,
                                  double progress, const QString &preset)
{
    TranscodeTask t;
    t.inputPath = QString("/Users/demo/Videos/%1").arg(fileName);
    t.outputPath = QString("/Users/demo/Output/%1_%2.mp4")
                       .arg(fileName.section('.', 0, 0), preset);
    t.presetName = preset;
    t.state = state;
    t.progress = progress;
    return t;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QEncoderTest");
    QCoreApplication::setApplicationName("QEncoderGuiCheck");

    g_theme = qEnvironmentVariable("QENCODER_GUI_CHECK_THEME", "dark");
    ThemeManager::apply(g_theme);
    EnvironmentCheck::initialize();

    // 每次运行使用独立的临时任务库，避免污染真实数据或上一次的演示数据
    QTemporaryDir taskDbDir;
    qputenv("QENCODER_TASK_DB", taskDbDir.filePath("tasks.db").toUtf8());

    MainWindow w;
    int failures = 0;

    Q_FOREACH(QToolBar *tb, w.findChildren<QToolBar *>()) {
        qDebug() << "toolbar:" << tb->toolButtonStyle() << "iconSize:" << tb->iconSize()
                 << "buttons:" << tb->findChildren<QToolButton *>().size();
        if (tb->toolButtonStyle() != Qt::ToolButtonTextBesideIcon)
            ++failures;
        Q_FOREACH(QToolButton *b, tb->findChildren<QToolButton *>()) {
            if (b->objectName() == "qt_toolbar_ext_button")
                continue; // Qt 内部扩展按钮，不算业务按钮
            bool hasIcon = !b->icon().isNull();
            qDebug() << "  button text=" << b->text()
                     << "icon set=" << hasIcon
                     << "objectName=" << b->objectName()
                     << "parent=" << (b->parentWidget() ? b->parentWidget()->metaObject()->className() : "");
            if (b->text().isEmpty())
                ++failures;
            if (!hasIcon)
                ++failures;
            // addWidget 的按钮必须自己设置 ToolButtonTextBesideIcon，否则只显示图标
            if (b->toolButtonStyle() != Qt::ToolButtonTextBesideIcon)
                ++failures;
        }
    }
    qDebug() << "failures:" << failures;

    const bool demo = qEnvironmentVariableIsSet("QENCODER_GUI_CHECK_DEMO");
    if (demo) {
        PresetManager::initialize();
        Settings::setTheme(g_theme);

        TaskQueue &queue = w.taskQueue();
        queue.setAutoStart(false);
        queue.addTask(makeDemoTask("海边日落.mp4", TranscodeTask::State::Waiting, 0, "H.264 1080P"));
        queue.addTask(makeDemoTask("旅行记录_2026.mp4", TranscodeTask::State::Running, 37,
                                   "H.264 1080P"));
        queue.addTask(makeDemoTask("产品演示.mov", TranscodeTask::State::Paused, 62, "H.264 1080P"));
        queue.addTask(makeDemoTask("家庭聚会.mkv", TranscodeTask::State::Done, 100, "H.264 1080P"));
        queue.addTask(makeDemoTask("监控片段.ts", TranscodeTask::State::Failed, 23, "H.264 1080P"));
        queue.addTask(makeDemoTask("音乐现场.webm", TranscodeTask::State::Cancelled, 18,
                                   "H.264 1080P"));
    }

    w.resize(demo ? QSize(1040, 660) : QSize(900, 600));
    saveShot(&w, "main");

    if (demo) {
        AddTasksDialog addDlg(&w.taskQueue(), &w);
        saveShot(&addDlg, "addtasks");

        PresetDialog presetDlg(&w);
        saveShot(&presetDlg, "presets");

        SettingsDialog settingsDlg(&w);
        saveShot(&settingsDlg, "settings");

        if (!PresetManager::presets().isEmpty()) {
            PresetEditor editor(PresetManager::presets().first(), &w);
            saveShot(&editor, "preseteditor");
        }

        // 模拟重启：新窗口应能从数据库恢复全部 6 个任务
        MainWindow reloaded;
        auto *reloadedTable = reloaded.findChild<QTableWidget *>(QStringLiteral("taskTable"));
        const int reloadedRows = reloadedTable ? reloadedTable->rowCount() : -1;
        qDebug() << "reloaded task rows:" << reloadedRows;
        if (reloadedRows != 6)
            ++failures;

        QSettings testSettings;
        testSettings.clear();
    }

    return failures == 0 ? 0 : 1;
}
