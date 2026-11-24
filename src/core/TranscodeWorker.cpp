#include "core/TranscodeWorker.hpp"
#include "core/EnvironmentCheck.hpp"
#include "core/MediaProbe.hpp"

#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

#ifdef Q_OS_UNIX
#include <signal.h>
#include <unistd.h>
#endif

namespace {

// "原比例 1080P" → 短边 1080、保持宽高比（竖屏/4:3 不再被拉伸）
QString scaleFilterFor(const QString &size)
{
    static const QRegularExpression re(QStringLiteral("^原比例\\s*(\\d+)\\s*[pP]$"));
    const QRegularExpressionMatch m = re.match(size.trimmed());
    if (!m.hasMatch())
        return {};
    const QString n = m.captured(1);
    return QStringLiteral("scale=%1:%1:force_original_aspect_ratio=increase:force_divisible_by=2")
        .arg(n);
}

bool isCopyCodec(const QString &codec)
{
    const QString c = codec.trimmed();
    return c.isEmpty() || c.compare(QStringLiteral("copy"), Qt::CaseInsensitive) == 0;
}

} // namespace

TranscodeWorker::TranscodeWorker(QObject *parent) : QObject(parent)
{
    connect(&m_proc, &QProcess::readyReadStandardOutput, this, &TranscodeWorker::onStdOut);
    connect(&m_proc, &QProcess::readyReadStandardError, this, &TranscodeWorker::onStdErr);
    connect(&m_proc, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &TranscodeWorker::onFinished);
    connect(&m_proc, &QProcess::errorOccurred, this, &TranscodeWorker::onProcessError);

    connect(&m_probe, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished), this,
            &TranscodeWorker::onProbeFinished);
    connect(&m_probe, &QProcess::errorOccurred, this, &TranscodeWorker::onProbeError);
    m_probeTimer.setSingleShot(true);
    connect(&m_probeTimer, &QTimer::timeout, this, &TranscodeWorker::onProbeTimeout);
}

bool TranscodeWorker::isRunning() const
{
    return m_proc.state() != QProcess::NotRunning || m_probe.state() != QProcess::NotRunning;
}

QStringList TranscodeWorker::buildArgs(const TranscodeTask &task, const TranscodePreset &preset,
                                       const QString &outputOverride)
{
    QStringList a;
    a << "-y" << "-i" << task.inputPath;

    auto appendIf = [&](const QString &key, const QString &val) {
        const QString v = val.trimmed();
        if (!v.isEmpty())
            a << key << v;
    };

    // 视频：空编码器或 "copy" 表示流拷贝；此时编码参数与缩放滤镜都无意义
    if (isCopyCodec(preset.vcodec)) {
        a << "-c:v" << "copy";
    } else {
        a << "-c:v" << preset.vcodec.trimmed();
        appendIf("-preset", preset.vpreset);
        appendIf("-crf", preset.crf);
        appendIf("-b:v", preset.vbitrate);
        QString size = preset.size.trimmed();
        if (!size.isEmpty() && size != QStringLiteral("原始")) {
            const QString scale = scaleFilterFor(size);
            if (!scale.isEmpty()) {
                a << "-vf" << scale;
            } else {
                size.replace('X', 'x');
                a << "-s" << size;
            }
        }
        appendIf("-r", preset.fps);
    }

    // 音频：同样支持空/"copy" = 流拷贝
    if (isCopyCodec(preset.acodec)) {
        a << "-c:a" << "copy";
    } else {
        a << "-c:a" << preset.acodec.trimmed();
        appendIf("-b:a", preset.abitrate);
        appendIf("-ar", preset.arate);
        if (preset.achannels > 0)
            a << "-ac" << QString::number(preset.achannels);
    }

    // 追加参数（支持带引号的参数，见 QProcess::splitCommand）
    a << QProcess::splitCommand(preset.extraArgs);

    // 进度输出到 stdout（pipe:1），供 UI 解析
    a << "-progress" << "pipe:1" << "-nostats";

    a << (outputOverride.isEmpty() ? task.outputPath : outputOverride);
    return a;
}

double TranscodeWorker::parseProgressSeconds(const QByteArray &line, double fallback)
{
    // out_time_us / out_time_ms 都是微秒；只按微秒换算一次
    if (line.startsWith("out_time_us=") || line.startsWith("out_time_ms="))
        return line.mid(12).toDouble() / 1000000.0;
    return fallback;
}

void TranscodeWorker::run(TranscodeTask task, const TranscodePreset &preset)
{
    m_task = task;
    m_preset = preset;
    m_duration = 0;
    m_lastOutTime = 0;
    m_speed = 0;
    m_paused = false;
    m_cancelled = false;
    m_doneEmitted = false;
    m_stdoutBuf.clear();
    m_stderrTail.clear();

    // 写入临时文件（保留正确扩展名，ffmpeg 才能推断 muxer），成功后原子替换最终文件。
    // 这样取消/失败不会破坏已存在的输出文件，也不会留下半成品。
    m_finalOutput = m_task.outputPath;
    const QFileInfo fi(m_finalOutput);
    if (fi.suffix().isEmpty())
        m_tempOutput = m_finalOutput + ".part";
    else
        m_tempOutput = fi.dir().filePath(fi.completeBaseName() + ".part." + fi.suffix());
    QDir().mkpath(fi.absolutePath());
    QFile::remove(m_tempOutput);

    if (!QFile::exists(m_task.inputPath)) {
        emitDone(TranscodeTask::State::Failed, tr("输入文件不存在"));
        return;
    }

    const QString ffprobe = EnvironmentCheck::ffprobePath();
    if (ffprobe.isEmpty()) {
        startFfmpeg(); // 没有 ffprobe 也能转码，只是没有进度百分比
        return;
    }

    m_probe.start(ffprobe, QStringList() << "-v" << "error" << "-print_format" << "json"
                                         << "-show_format" << "-show_streams" << m_task.inputPath);
    m_probeTimer.start(10000); // 探测超时保护：慢文件不再长期占用并发槽
}

void TranscodeWorker::startFfmpeg()
{
    if (m_cancelled) {
        emitDone(TranscodeTask::State::Cancelled);
        return;
    }
    const QString ffmpeg = EnvironmentCheck::ffmpegPath();
    if (ffmpeg.isEmpty()) {
        emitDone(TranscodeTask::State::Failed, tr("未找到 ffmpeg"));
        return;
    }
    m_proc.start(ffmpeg, buildArgs(m_task, m_preset, m_tempOutput));
    m_elapsed.start();
}

void TranscodeWorker::onProbeFinished(int, QProcess::ExitStatus)
{
    m_probeTimer.stop();
    const QByteArray out = m_probe.readAllStandardOutput();
    if (m_cancelled) {
        emitDone(TranscodeTask::State::Cancelled);
        return;
    }
    const MediaInfo info = MediaProbe::parseOutput(out);
    m_duration = info.duration.toDouble();
    startFfmpeg();
}

void TranscodeWorker::onProbeError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return; // 崩溃/被杀随后会触发 finished
    m_probeTimer.stop();
    if (m_cancelled)
        emitDone(TranscodeTask::State::Cancelled);
    else
        startFfmpeg();
}

void TranscodeWorker::onProbeTimeout()
{
    if (m_probe.state() != QProcess::NotRunning)
        m_probe.kill(); // finished 回调里会继续启动 ffmpeg
}

void TranscodeWorker::onStdOut()
{
    if (m_task.id.isEmpty())
        return;

    m_stdoutBuf += m_proc.readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl);
        m_stdoutBuf.remove(0, nl + 1);
        m_lastOutTime = parseProgressSeconds(line, m_lastOutTime);
        if (line.startsWith("speed=")) {
            QString s = QString::fromLatin1(line.mid(6)).trimmed();
            if (s.endsWith('x'))
                s.chop(1);
            bool ok = false;
            const double v = s.toDouble(&ok);
            if (ok)
                m_speed = v;
        }
    }

    if (m_duration > 0) {
        const double pct = qBound(0.0, (m_lastOutTime / m_duration) * 100.0, 100.0);
        emit progress(m_task.id, pct, m_elapsed.isValid() ? m_elapsed.elapsed() : 0, m_speed);
    }
}

void TranscodeWorker::onStdErr()
{
    m_stderrTail += m_proc.readAllStandardError();
    if (m_stderrTail.size() > 8192)
        m_stderrTail = m_stderrTail.right(4096);
}

void TranscodeWorker::onFinished(int exitCode, QProcess::ExitStatus status)
{
    onStdOut(); // 读完剩余进度输出
    onStdErr();

    if (m_cancelled) {
        QFile::remove(m_tempOutput); // 清理本次产生的半成品
        emitDone(TranscodeTask::State::Cancelled);
        return;
    }

    if (status == QProcess::NormalExit && exitCode == 0 && QFile::exists(m_tempOutput)) {
        if (replaceOutput())
            emitDone(TranscodeTask::State::Done);
        else
            emitDone(TranscodeTask::State::Failed, tr("无法写入输出文件：") + m_finalOutput);
        return;
    }

    QFile::remove(m_tempOutput); // 失败：清理半成品
    const QString tail = QString::fromLocal8Bit(m_stderrTail.right(400)).trimmed();
    if (status != QProcess::NormalExit)
        emitDone(TranscodeTask::State::Failed, tr("进程异常结束：") + tail);
    else
        emitDone(TranscodeTask::State::Failed,
                 tr("ffmpeg 退出码 %1：%2").arg(exitCode).arg(tail));
}

bool TranscodeWorker::replaceOutput()
{
    // 先删旧目标再改名：Windows 上 rename 不能覆盖已存在文件
    if (QFileInfo::exists(m_finalOutput) && !QFile::remove(m_finalOutput))
        return false;
    return QFile::rename(m_tempOutput, m_finalOutput);
}

void TranscodeWorker::onProcessError(QProcess::ProcessError error)
{
    if (error != QProcess::FailedToStart)
        return;
    if (m_cancelled)
        emitDone(TranscodeTask::State::Cancelled);
    else
        emitDone(TranscodeTask::State::Failed, tr("启动 ffmpeg 失败：") + m_proc.errorString());
}

void TranscodeWorker::cancel()
{
    if (m_doneEmitted)
        return;
    m_cancelled = true;
    m_probeTimer.stop();

    // 兜底：若平台未回报 finished，保证任务不会永远占着并发槽
    QTimer::singleShot(5000, this, [this] { emitDone(TranscodeTask::State::Cancelled); });

    if (m_probe.state() != QProcess::NotRunning) {
        m_probe.kill();
        return; // onProbeFinished 里统一发出 Cancelled
    }

    if (m_proc.state() != QProcess::NotRunning) {
#ifdef Q_OS_UNIX
        if (m_paused)
            kill(m_proc.processId(), SIGCONT); // 停止的进程需先继续才会处理 SIGTERM
#endif
        m_proc.terminate();
        QTimer::singleShot(2000, &m_proc, [this] {
            if (m_proc.state() != QProcess::NotRunning)
                m_proc.kill();
        });
        return;
    }

    emitDone(TranscodeTask::State::Cancelled);
}

bool TranscodeWorker::pause()
{
#ifdef Q_OS_UNIX
    if (m_proc.state() == QProcess::Running) {
        m_paused = true;
        kill(m_proc.processId(), SIGSTOP);
        return true;
    }
#endif
    return false; // 平台不支持，或 ffmpeg 尚未真正启动
}

bool TranscodeWorker::resume()
{
#ifdef Q_OS_UNIX
    if (m_proc.state() == QProcess::Running) {
        m_paused = false;
        kill(m_proc.processId(), SIGCONT);
        return true;
    }
#endif
    m_paused = false;
    return false;
}

void TranscodeWorker::emitDone(TranscodeTask::State state, const QString &error)
{
    if (m_doneEmitted)
        return;
    m_doneEmitted = true;
    m_probeTimer.stop();
    emit done(m_task.id, state, error);
}
