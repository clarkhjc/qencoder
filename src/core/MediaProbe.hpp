#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>

// 步骤 3（顺带）：输入探测——调用 ffprobe 取输入信息（时长/流/分辨率/音频参数），
// 供 PresetEditor 可视化参数预览与后续转码工作使用
struct MediaInfo
{
    bool ok = false;
    QString duration;          // "12.345"
    bool hasVideo = false;
    QString videoCodec;        // 如 "h264"
    int width = 0, height = 0;
    double fps = 0;            // 平均帧率
    QString vPixelFormat;
    bool hasAudio = false;
    QString audioCodec;        // 如 "aac"
    int sampleRate = 0;
    int channels = 0;
    QString fileName;          // 便于显示
};

class MediaProbe
{
public:
    // 同步探测（仅用于测试或非 GUI 线程；GUI 转码路径使用 parseOutput 异步解析，
    // 避免在界面线程等待 ffprobe 最长 30 秒）
    static MediaInfo probe(const QString &inputPath);

    // 解析 ffprobe -print_format json -show_format -show_streams 的输出
    static MediaInfo parseOutput(const QByteArray &json);
};
