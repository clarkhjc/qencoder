#pragma once

#include "core/TranscodePreset.hpp"
#include "core/TranscodeTask.hpp"

#include <QByteArray>
#include <QElapsedTimer>
#include <QProcess>
#include <QTimer>

// 步骤 5：单任务转码 worker——调用 ffmpeg CLI 完成一个任务
// 探测（ffprobe）与转码（ffmpeg）均为异步启动，不阻塞 GUI 线程。
class TranscodeWorker : public QObject
{
    Q_OBJECT
public:
    explicit TranscodeWorker(QObject *parent = nullptr);

    // 启动任务
    void run(TranscodeTask task, const TranscodePreset &preset);

    bool isRunning() const;

    // 取消：ffprobe 阶段直接终止；ffmpeg 阶段先 SIGTERM，2 秒后仍未退出则 SIGKILL
    void cancel();

    // 暂停：macOS/Linux 用 SIGSTOP 冻结进程；不支持的平台返回 false
    bool pause();
    bool resume();

    // 构建 ffmpeg 参数（static，便于单元测试）。
    // outputOverride 非空时写入该路径（worker 用 <output>.part 临时文件，成功后原子重命名）。
    static QStringList buildArgs(const TranscodeTask &task, const TranscodePreset &preset,
                                 const QString &outputOverride = QString());

    // 解析 ffmpeg -progress 输出中的时间行。
    // 注意：ffmpeg 的 out_time_us 与 out_time_ms 都是微秒（后者是历史遗留命名）。
    static double parseProgressSeconds(const QByteArray &line, double fallback);

signals:
    void done(const QString &id, TranscodeTask::State state, QString error);
    void progress(const QString &id, double pct, qint64 elapsedMs, double speed);

private slots:
    void onStdOut();
    void onStdErr();
    void onProbeFinished(int exitCode, QProcess::ExitStatus status);
    void onProbeError(QProcess::ProcessError error);
    void onProbeTimeout();
    void onFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);

private:
    void startFfmpeg();
    void emitDone(TranscodeTask::State state, const QString &error = {});
    bool replaceOutput(); // .part -> 最终路径（失败/取消时清理 .part）

    QProcess m_proc;            // ffmpeg
    QProcess m_probe;           // ffprobe
    QTimer m_probeTimer;        // ffprobe 超时保护
    TranscodeTask m_task;
    TranscodePreset m_preset;
    QByteArray m_stdoutBuf;     // 行缓冲，避免跨数据块解析半行
    QByteArray m_stderrTail;    // 只保留 stderr 尾部，避免无限增长
    double m_duration = 0;      // 秒
    double m_lastOutTime = 0;   // 已转码时长（秒）
    double m_speed = 0;         // ffmpeg speed（倍速，0=未知）
    QElapsedTimer m_elapsed;    // 本次 ffmpeg 墙钟耗时
    bool m_paused = false;
    bool m_cancelled = false;
    bool m_doneEmitted = false;
    QString m_finalOutput;      // 用户期望的输出路径
    QString m_tempOutput;       // 实际写入的临时文件（.part）
};
