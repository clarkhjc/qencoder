#include <utility>
#include "core/EnvironmentCheck.hpp"
#include "settings/Settings.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

namespace {

// 按优先级搜索：设置里指定的目录 → 环境变量 → 应用目录 → PATH → 常见安装目录
QString findExecutable(const QString &name)
{
    QStringList dirs;
    const QString configured = Settings::ffmpegDir().trimmed();
    if (!configured.isEmpty())
        dirs << configured;
    const QString envDir = qEnvironmentVariable("QENCODER_FFMPEG_DIR").trimmed();
    if (!envDir.isEmpty())
        dirs << envDir;
    dirs << QCoreApplication::applicationDirPath();

    const QString pathEnv = qEnvironmentVariable("PATH");
    if (!pathEnv.isEmpty())
        dirs << pathEnv.split(QDir::listSeparator(), Qt::SkipEmptyParts);

#if defined(Q_OS_MACOS)
    // 从 Finder 启动 GUI 时 PATH 不含 Homebrew/MacPorts 目录
    dirs << "/opt/homebrew/bin" << "/usr/local/bin" << "/opt/local/bin";
#elif defined(Q_OS_LINUX)
    dirs << "/usr/local/bin" << "/usr/bin" << "/snap/bin";
#endif

    for (const QString &dir : std::as_const(dirs)) {
        if (dir.trimmed().isEmpty())
            continue;
        const QString candidate = QDir(dir).filePath(name);
        if (QFileInfo::exists(candidate) && QFileInfo(candidate).isExecutable())
            return QFileInfo(candidate).absoluteFilePath();
    }

    const QString found = QStandardPaths::findExecutable(name);
    return found.isEmpty() ? QString() : QFileInfo(found).absoluteFilePath();
}

} // namespace

QString EnvironmentCheck::m_ffmpegPath;
QString EnvironmentCheck::m_ffprobePath;
QString EnvironmentCheck::m_ffmpegVersion;

const QString &EnvironmentCheck::ffmpegPath() { return m_ffmpegPath; }
const QString &EnvironmentCheck::ffprobePath() { return m_ffprobePath; }
const QString &EnvironmentCheck::ffmpegVersion() { return m_ffmpegVersion; }

bool EnvironmentCheck::isValid()
{
    return !m_ffmpegPath.isEmpty() && !m_ffprobePath.isEmpty();
}

QString EnvironmentCheck::findTool(const QString &name)
{
    QString p = findExecutable(name);
    if (p.isEmpty())
        qWarning() << "未找到可用的" << name;
    return p;
}

QString EnvironmentCheck::runVersion(const QString &path)
{
    if (path.isEmpty())
        return {};

    QProcess proc;
    proc.start(path, QStringList() << "-version");
    if (!proc.waitForStarted(1500)) {
        qWarning() << path << "启动失败:" << proc.errorString();
        return {};
    }
    proc.waitForFinished(2500);

    QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    // 第一行形如 "ffmpeg version 9.0.2 Copyright ..."
    int i = out.indexOf("ffmpeg version ");
    if (i >= 0) {
        int start = i + 15;
        int end = out.indexOf(' ', start);
        return end > start ? out.mid(start, end - start) : QString();
    }
    return {};
}

void EnvironmentCheck::initialize()
{
    m_ffmpegPath = findTool("ffmpeg");
    m_ffprobePath = findTool("ffprobe");
    m_ffmpegVersion = runVersion(m_ffmpegPath);
}
