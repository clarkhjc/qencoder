#pragma once

#include <QString>

// 步骤 1：环境检查——ffmpeg/ffprobe 的路径解析与版本检测
class EnvironmentCheck
{
public:
    static void initialize();

    static const QString &ffmpegPath();
    static const QString &ffprobePath();
    static const QString &ffmpegVersion(); // e.g. "9.0.2"，不可用时为空串

    static bool isValid(); // ffmpeg 与 ffprobe 都可用

private:
    static QString findTool(const QString &name);
    static QString runVersion(const QString &path);

    static QString m_ffmpegPath;
    static QString m_ffprobePath;
    static QString m_ffmpegVersion;
};
