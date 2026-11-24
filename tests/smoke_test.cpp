// 步骤 7：集成测试（顺带放置在 tests/，供 CTest 运行）
// - MediaProbe 探测（生成的测试视频）
// - TranscodeWorker::buildArgs 参数检查（含流拷贝与等比缩放）
// - 进度解析（ffmpeg 的 out_time_ms 也是微秒）
// - TaskQueue 基础状态机（自动并发、取消、移除、输入缺失失败）
// - TranscodeWorker 端到端转码（输出文件存在）
#include "core/EnvironmentCheck.hpp"
#include "core/MediaProbe.hpp"
#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"
#include "core/TaskQueue.hpp"
#include "core/TranscodeWorker.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

static int failures = 0;
#define CHECK(cond, msg)                                                                       \
    do {                                                                                       \
        if (!(cond)) {                                                                         \
            qDebug() << "FAIL:" << msg;                                                        \
            ++failures;                                                                        \
        } else {                                                                               \
            qDebug() << "PASS:" << msg;                                                        \
        }                                                                                      \
    } while (0)

#ifdef Q_OS_UNIX
static bool writeExecutable(const QString &path, const QByteArray &content)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(content) != content.size())
        return false;
    f.close();
    return f.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner
                            | QFileDevice::ExeOwner | QFileDevice::ReadGroup
                            | QFileDevice::ExeGroup | QFileDevice::ReadOther
                            | QFileDevice::ExeOther);
}
#endif

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName("QEncoderTest");
    QCoreApplication::setApplicationName("QEncoderSmokeTest");

    // 1. 环境检测
    EnvironmentCheck::initialize();
    CHECK(!EnvironmentCheck::ffmpegPath().isEmpty(), "ffmpeg 路径解析");
    qDebug() << "ffmpeg:" << EnvironmentCheck::ffmpegPath() << EnvironmentCheck::ffmpegVersion();

    // 2. 生成测试素材
    QTemporaryDir dir;
    QString input = dir.filePath("sample.mp4");
    QStringList genArgs = QStringList()
                          << "-y" << "-f" << "lavfi" << "-i"
                          << "testsrc=duration=2:size=320x240:rate=10"
                          << "-f" << "lavfi" << "-i" << "sine=frequency=440:duration=2"
                          << "-c:v" << "libx264" << "-pix_fmt" << "yuv420p"
                          << "-c:a" << "aac" << "-shortest" << input;
    QProcess::execute(EnvironmentCheck::ffmpegPath(), genArgs);
    CHECK(QFileInfo::exists(input), "测试视频生成");

    // 3. MediaProbe 探测
    MediaInfo info = MediaProbe::probe(input);
    CHECK(info.ok, "ffprobe 探测 ok");
    CHECK(info.hasVideo, "探测到视频流");
    CHECK(info.hasAudio, "探测到音频流");
    qDebug() << "probe duration:" << info.duration << "video:" << info.videoCodec << info.width
             << "x" << info.height << "fps:" << info.fps;

    // 4. buildArgs 检查
    TranscodeTask t;
    t.inputPath = input;
    t.outputPath = dir.filePath("out.mp4");
    TranscodePreset p;
    p.vcodec = "libx264";
    p.crf = "23";
    p.vpreset = "medium";
    p.size = "1920x1080";
    p.acodec = "aac";
    p.abitrate = "128k";
    p.arate = "44100";
    QStringList args = TranscodeWorker::buildArgs(t, p);
    qDebug() << "buildArgs:" << args;
    CHECK(args.contains("-c:v") && args.at(args.indexOf("-c:v") + 1) == "libx264", "vcodec 参数");
    CHECK(args.contains("-crf"), "crf 参数");
    CHECK(args.contains("-s") && args.at(args.indexOf("-s") + 1) == "1920x1080", "size 参数");
    CHECK(args.last() == t.outputPath, "输出路径在末尾");

    // 4b. 空编码器 = 流拷贝；原比例 1080P 生成等比缩放滤镜
    {
        TranscodePreset copyPreset;
        copyPreset.size = "1920x1080";
        const QStringList copyArgs = TranscodeWorker::buildArgs(t, copyPreset);
        CHECK(copyArgs.contains("-c:v") && copyArgs.at(copyArgs.indexOf("-c:v") + 1) == "copy",
              "空视频编码器按 copy 处理");
        CHECK(!copyArgs.contains("-vf") && !copyArgs.contains("-s"),
              "流拷贝时不生成缩放参数");

        TranscodePreset scaled;
        scaled.vcodec = "libx264";
        scaled.size = "原比例 1080P";
        const QStringList scaleArgs = TranscodeWorker::buildArgs(t, scaled);
        const int vf = scaleArgs.indexOf("-vf");
        CHECK(vf >= 0 && scaleArgs.at(vf + 1).contains("force_original_aspect_ratio=increase"),
              "原比例 1080P 使用等比缩放");
    }

    // 4c. 进度解析：out_time_us 与 out_time_ms 都是微秒
    CHECK(qFuzzyCompare(TranscodeWorker::parseProgressSeconds("out_time_us=1500000", 0.0), 1.5),
          "out_time_us 解析为秒");
    CHECK(qFuzzyCompare(TranscodeWorker::parseProgressSeconds("out_time_ms=1500000", 0.0), 1.5),
          "out_time_ms 按微秒解析");

    // 4d. 队列：0 并发 = 自动（CPU-1）；等待任务可取消/移除
    {
        TaskQueue q;
        const int expected = qMax(1, QThread::idealThreadCount() - 1);
        CHECK(q.maxParallel() == expected, "默认并发为 CPU-1");
        q.setMaxParallel(0);
        CHECK(q.maxParallel() == expected, "0 表示自动并发");

        q.setAutoStart(false);
        TranscodeTask qt;
        qt.inputPath = input;
        qt.outputPath = dir.filePath("queued.mp4");
        q.addTask(qt);
        const QString id = q.tasks().first().id;
        CHECK(!id.isEmpty(), "自动生成任务 ID");
        q.cancelTask(id);
        CHECK(q.tasks().first().state == TranscodeTask::State::Cancelled, "等待中的任务可取消");
        CHECK(q.removeTask(0), "已取消任务可移除");
        CHECK(q.tasks().isEmpty(), "任务列表已清空");
    }

    // 4d2. 全部删除：清空任务列表
    {
        TaskQueue q;
        q.setAutoStart(false);
        TranscodeTask a;
        a.inputPath = input;
        a.outputPath = dir.filePath("clear1.mp4");
        TranscodeTask b;
        b.inputPath = input;
        b.outputPath = dir.filePath("clear2.mp4");
        q.addTask(a);
        q.addTask(b);
        bool cleared = false;
        QObject::connect(&q, &TaskQueue::tasksCleared, [&] { cleared = true; });
        q.clearAll();
        CHECK(cleared && q.tasks().isEmpty(), "全部删除清空任务列表");
    }

    // 4d3. 任务持久化：新增/状态/删除/清空，以及中断任务重新排队
    {
        const QString dbPath = dir.filePath("tasks.db");
        QString cancelledId;
        {
            TaskQueue q;
            CHECK(q.loadFromStore(dbPath), "打开任务数据库");

            q.setAutoStart(false);
            TranscodeTask a;
            a.inputPath = input;
            a.outputPath = dir.filePath("persist1.mp4");
            q.addTask(a);
            cancelledId = q.tasks().first().id;
            q.cancelTask(cancelledId);

            TranscodeTask b;
            b.inputPath = input;
            b.outputPath = dir.filePath("persist2.mp4");
            b.state = TranscodeTask::State::Done;
            b.progress = 100;
            q.addTask(b);

            TranscodeTask c;
            c.inputPath = input;
            c.outputPath = dir.filePath("persist3.mp4");
            c.state = TranscodeTask::State::Running;
            c.progress = 42;
            q.addTask(c);
        }
        {
            TaskQueue q;
            CHECK(q.loadFromStore(dbPath), "重新打开任务数据库");
            CHECK(q.tasks().size() == 3, "历史任务数量恢复");
            bool cancelledOk = false;
            bool doneOk = false;
            bool requeuedOk = false;
            for (const TranscodeTask &t : q.tasks()) {
                if (t.id == cancelledId && t.state == TranscodeTask::State::Cancelled)
                    cancelledOk = true;
                if (t.state == TranscodeTask::State::Done && t.progress == 100.0)
                    doneOk = true;
                if (t.state == TranscodeTask::State::Waiting && t.progress == 0.0)
                    requeuedOk = true;
            }
            CHECK(cancelledOk, "已取消状态持久化");
            CHECK(doneOk, "已完成状态与进度持久化");
            CHECK(requeuedOk, "中断任务重新排队");
            q.removeTask(0);
        }
        {
            TaskQueue q;
            CHECK(q.loadFromStore(dbPath), "再次打开任务数据库");
            CHECK(q.tasks().size() == 2, "移除任务已持久化");
            q.clearAll();
        }
        {
            TaskQueue q;
            CHECK(q.loadFromStore(dbPath), "清空后打开任务数据库");
            CHECK(q.tasks().isEmpty(), "全部删除已持久化");
        }
    }

    // 4e. 队列：输入缺失时立即失败
    {
        TaskQueue q;
        TranscodeTask bad;
        bad.inputPath = dir.filePath("does-not-exist.mp4");
        bad.outputPath = dir.filePath("bad.mp4");
        TranscodeTask::State finalState = TranscodeTask::State::Waiting;
        bool finished = false;
        QObject::connect(&q, &TaskQueue::taskFinished,
                         [&](const QString &, TranscodeTask::State st, QString) {
                             finalState = st;
                             finished = true;
                         });
        q.addTask(bad);
        CHECK(finished && finalState == TranscodeTask::State::Failed, "缺少输入文件时任务失败");
    }

    // 4f. 预设解析：按名称查找；默认预设遵循设置
    {
        QSettings clean;
        clean.clear(); // 独立的测试配置域，避免影响真实设置
        PresetManager::initialize();
        CHECK(!PresetManager::presets().isEmpty(), "内置预设加载");

        TranscodePreset cp;
        cp.name = "Copy Only";
        cp.container = "mp4";
        cp.vcodec = "copy";
        cp.acodec = "copy";
        PresetManager::addPreset(cp);
        CHECK(PresetManager::presetByName("copy only").name == cp.name,
              "预设按名称（忽略大小写）查找");
        const TranscodePreset rt =
            PresetManager::presetFromJson(PresetManager::presetToJson(cp));
        CHECK(rt.name == cp.name && rt.vcodec == cp.vcodec, "预设快照 JSON 往返");
        Settings::setDefaultPresetName(cp.name);
        CHECK(PresetManager::defaultPreset().name == cp.name, "默认预设读取设置");
        Settings::setDefaultPresetName(QString());
    }

    // 4g. 队列按任务所选预设执行（流拷贝）
    {
        TaskQueue q;
        TranscodeTask ct;
        ct.inputPath = input;
        ct.outputPath = dir.filePath("copy_out.mp4");
        ct.presetName = "Copy Only";
        TranscodeTask::State finalState = TranscodeTask::State::Waiting;
        bool finished = false;
        QObject::connect(&q, &TaskQueue::taskFinished,
                         [&](const QString &, TranscodeTask::State st, QString) {
                             finalState = st;
                             finished = true;
                         });
        q.setAutoStart(false);
        q.addTask(ct);
        CHECK(!q.tasks().first().presetJson.isEmpty(), "任务保存预设快照");
        q.start();

        QEventLoop loop;
        QObject::connect(&q, &TaskQueue::taskFinished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        CHECK(finished && finalState == TranscodeTask::State::Done, "队列按任务预设完成转码");
        CHECK(QFileInfo::exists(ct.outputPath), "队列转码输出存在");
    }

    // 4h. 取消刚启动（探测阶段）的任务 → Cancelled，不残留输出
    {
        TaskQueue q;
        TranscodeTask ct;
        ct.inputPath = input;
        ct.outputPath = dir.filePath("cancel_out.mp4");
        q.setAutoStart(false);
        q.addTask(ct);
        const QString id = q.tasks().first().id;
        TranscodeTask::State finalState = TranscodeTask::State::Running;
        bool finished = false;
        QObject::connect(&q, &TaskQueue::taskFinished,
                         [&](const QString &, TranscodeTask::State st, QString) {
                             finalState = st;
                             finished = true;
                         });
        q.start();
        q.cancelTask(id);

        QEventLoop loop;
        QObject::connect(&q, &TaskQueue::taskFinished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        CHECK(finished && finalState == TranscodeTask::State::Cancelled,
              "取消任务最终状态为 Cancelled");
        CHECK(!QFileInfo::exists(ct.outputPath), "取消后不残留输出文件");
        CHECK(QDir(dir.path()).entryList(QStringList() << "*part*", QDir::Files).isEmpty(),
              "取消后不残留临时文件");
    }

    // 4i. 运行中的任务被取消：worker 的失败退出码不能把状态覆盖回 Failed
#ifdef Q_OS_UNIX
    {
        const QString fakeDir = dir.filePath("fakebin");
        QDir().mkpath(fakeDir);
        const bool ok1 = writeExecutable(
            fakeDir + "/ffmpeg",
            "#!/bin/sh\n"
            "if [ \"$1\" = \"-version\" ]; then echo 'ffmpeg version 9.9.9 fake'; exit 0; fi\n"
            "trap 'exit 143' TERM INT\n"
            "i=0\n"
            "while [ $i -lt 40 ]; do sleep 0.05; i=$((i+1)); done\n"
            "exit 1\n");
        const bool ok2 = writeExecutable(
            fakeDir + "/ffprobe",
            "#!/bin/sh\necho '{\"format\":{\"duration\":\"10.0\"},\"streams\":[]}'\nexit 0\n");
        CHECK(ok1 && ok2, "测试用假 ffmpeg/ffprobe 创建");

        qputenv("QENCODER_FFMPEG_DIR", fakeDir.toUtf8());
        EnvironmentCheck::initialize();

        TaskQueue q;
        TranscodeTask ft;
        ft.inputPath = input;
        ft.outputPath = dir.filePath("fake_out.mp4");
        q.setAutoStart(false);
        q.addTask(ft);
        const QString fid = q.tasks().first().id;
        TranscodeTask::State finalState = TranscodeTask::State::Running;
        bool finished = false;
        QObject::connect(&q, &TaskQueue::taskFinished,
                         [&](const QString &, TranscodeTask::State st, QString) {
                             finalState = st;
                             finished = true;
                         });
        q.start();
        // 等 ffmpeg 真正启动后再取消，覆盖"失败退出码覆盖已取消状态"的路径
        QTimer::singleShot(300, &q, [&q, fid] { q.cancelTask(fid); });

        QEventLoop loop;
        QObject::connect(&q, &TaskQueue::taskFinished, &loop, &QEventLoop::quit);
        QTimer::singleShot(5000, &loop, &QEventLoop::quit);
        loop.exec();
        CHECK(finished && finalState == TranscodeTask::State::Cancelled,
              "运行中取消后状态保持 Cancelled");
        CHECK(!QFileInfo::exists(ft.outputPath), "运行中取消不残留输出");

        qunsetenv("QENCODER_FFMPEG_DIR");
        EnvironmentCheck::initialize(); // 恢复真实 ffmpeg
    }
#endif

    {
        QSettings cleanup;
        cleanup.clear();
    }

    // 5. 端到端转码
    TranscodeWorker w;
    bool okDone = false;
    bool outExists = false;
    QObject::connect(&w, &TranscodeWorker::done, [&](const QString &id, TranscodeTask::State st,
                                                     QString err) {
        qDebug() << "done:" << id << static_cast<int>(st) << err;
        outExists = QFileInfo(t.outputPath).exists();
        okDone = (st == TranscodeTask::State::Done);
        app.exit(0);
    });
    QTimer::singleShot(60000, [&] {
        qDebug() << "timeout waiting ffmpeg";
        app.exit(2);
    });
    w.run(t, p);
    app.exec();
    CHECK(okDone, "转码完成状态");
    CHECK(outExists, "输出文件存在");
    CHECK(QDir(dir.path()).entryList(QStringList() << "*part*", QDir::Files).isEmpty(),
          "成功转码后不残留临时文件");

    qDebug() << "---- failures:" << failures;
    return failures == 0 ? 0 : 1;
}
