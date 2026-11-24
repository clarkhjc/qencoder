#pragma once

#include <QString>

// 步骤 2：可视化参数预设实体
// 编码器名直接传给 ffmpeg -c:v / -c:a（不额外校验）；空串或 "copy" 表示流拷贝。
// size 支持 空/"原始"、"1920x1080"、"原比例 1080P"（短边按宽高比缩放）
struct TranscodePreset
{
    QString name;        // 名称（如 "H.264 1080P 转码"）

    // 输出容器与音视频编码
    QString container;   // mp4/mkv/webm/mov/flv/...
    QString vcodec;      // 视频编码器（libx264/libx265/libvpx-vp9/libsvtav1/...），空串/copy=拷贝
    QString acodec;      // 音频编码器（aac/libopus/...），空串/copy=拷贝

    // 视频参数
    QString vpreset;     // ffmpeg -preset（medium/slow/veryslow），空串=未设置
    QString crf;         // 质量参数（如 "23"），空串=未设置
    QString vbitrate;    // 视频码率（如 "3000k"），空串=未设置
    QString size;        // 分辨率（如 "1920x1080" 或 "原比例 1080P"），空串=保持原分辨率
    QString fps;         // 帧率（如 "30"），空串=未设置

    // 音频参数
    QString abitrate;    // 音频码率（如 "128k"），空串=默认
    QString arate;       // 采样率（如 "44100"），空串=默认
    int achannels = 0;   // 声道数（0=默认）

    QString extraArgs;   // 追加自定义参数（如 "-movflags +faststart"）
    QString description; // 可选说明
};
