#include "core/EncoderRegistry.hpp"
#include "core/EnvironmentCheck.hpp"

#include <QProcess>
#include <QRegularExpression>

namespace {

struct EncoderCache
{
    QSet<QString> set;
    bool loaded = false;
};

EncoderCache &cache()
{
    static EncoderCache c;
    return c;
}

void loadIfNeeded()
{
    EncoderCache &c = cache();
    if (c.loaded)
        return;
    c.loaded = true;

    if (EnvironmentCheck::ffmpegPath().isEmpty())
        return;

    QProcess proc;
    proc.start(EnvironmentCheck::ffmpegPath(), QStringList() << "-hide_banner" << "-encoders");
    if (!proc.waitForStarted(1500) || !proc.waitForFinished(4000))
        return;

    const QString out = QString::fromLocal8Bit(proc.readAllStandardOutput());
    static const QRegularExpression re(QStringLiteral("^\\s*[A-Z.]{6}\\s+(\\S+)"));
    for (const QString &line : out.split('\n')) {
        const auto m = re.match(line);
        if (!m.hasMatch())
            continue;
        const QString name = m.captured(1);
        if (name != "=") // 跳过 "= Video/Subtitle..." 标题行
            c.set.insert(name);
    }
}

} // namespace

QSet<QString> EncoderRegistry::encoders()
{
    loadIfNeeded();
    return cache().set;
}

bool EncoderRegistry::isKnown(const QString &codec)
{
    const QString c = codec.trimmed();
    if (c.isEmpty() || c.compare(QStringLiteral("copy"), Qt::CaseInsensitive) == 0)
        return true;
    loadIfNeeded();
    if (cache().set.isEmpty())
        return true; // 无法获取编码器列表时不阻塞保存
    return cache().set.contains(c);
}

void EncoderRegistry::refresh()
{
    cache().set.clear();
    cache().loaded = false;
}
