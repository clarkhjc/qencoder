#include "core/MediaProbe.hpp"

#include "core/EnvironmentCheck.hpp"

#include <QDebug>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>

MediaInfo MediaProbe::parseOutput(const QByteArray &json)
{
    MediaInfo info;

    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return info;

    QJsonObject format = doc.object().value("format").toObject();
    info.duration = format.value("duration").toString();

    for (const QJsonValue &v : doc.object().value("streams").toArray()) {
        QJsonObject s = v.toObject();
        QString type = s.value("codec_type").toString();
        if (type == "video" && !info.hasVideo) {
            info.hasVideo = true;
            info.videoCodec = s.value("codec_name").toString();
            info.width = s.value("width").toInt();
            info.height = s.value("height").toInt();
            info.vPixelFormat = s.value("pix_fmt").toString();
            // avg_frame_rate 形如 "30000/1001" 或 "0/0"
            QStringList fr = s.value("avg_frame_rate").toString().split('/');
            if (fr.size() == 2 && fr.at(1).toDouble() > 0)
                info.fps = fr.at(0).toDouble() / fr.at(1).toDouble();
        } else if (type == "audio" && !info.hasAudio) {
            info.hasAudio = true;
            info.audioCodec = s.value("codec_name").toString();
            info.sampleRate = s.value("sample_rate").toInt();
            info.channels = s.value("channels").toInt();
        }
    }

    info.ok = !info.duration.isEmpty();
    return info;
}

MediaInfo MediaProbe::probe(const QString &inputPath)
{
    MediaInfo info;
    info.fileName = QFileInfo(inputPath).fileName();

    const QString ffprobe = EnvironmentCheck::ffprobePath();
    if (ffprobe.isEmpty() || !QFileInfo::exists(inputPath))
        return info;

    QProcess proc;
    proc.start(ffprobe, QStringList()
                            << "-v" << "error"
                            << "-print_format" << "json"
                            << "-show_format" << "-show_streams"
                            << inputPath);
    if (!proc.waitForStarted(3000) || !proc.waitForFinished(30000)) {
        qWarning() << "ffprobe 失败" << inputPath << proc.errorString();
        return info;
    }

    info = parseOutput(proc.readAllStandardOutput());
    info.fileName = QFileInfo(inputPath).fileName();
    return info;
}
