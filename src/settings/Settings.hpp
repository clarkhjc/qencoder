#pragma once

#include <QSettings>
#include <QString>

// 步骤 7 补充：全局设置（保存在 QSettings）
// - 默认输出目录（用于目录批量添加/自动命名）
// - 最大并发数（0 = 自动，CPU 核数-1）
// - 默认预设（按名称，批量添加时预选）
// - 系统通知开关（完成/失败）
class Settings
{
public:
    static void initialize();

    static QString outputDir();
    static void setOutputDir(const QString &dir);

    static int maxParallel();
    static void setMaxParallel(int n);

    static QString defaultPresetName();
    static void setDefaultPresetName(const QString &name);

    // ffmpeg/ffprobe 所在目录（空 = 自动搜索 PATH 与常见安装目录）
    static QString ffmpegDir();
    static void setFfmpegDir(const QString &dir);

    static bool systemNotifications();
    static void setSystemNotifications(bool on);

    // 主题（dark/light/pink，默认 dark）
    static QString theme();
    static void setTheme(const QString &name);

private:
    static QSettings &settings();
};
