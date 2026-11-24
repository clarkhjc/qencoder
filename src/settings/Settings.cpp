#include "settings/Settings.hpp"

#include <QApplication>
#include <QCoreApplication>

QSettings &Settings::settings()
{
    static QSettings s(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    return s;
}

void Settings::initialize()
{
    // 首次运行：默认开启系统通知
    if (!settings().contains("settings/notifications"))
        settings().setValue("settings/notifications", true);
}

QString Settings::outputDir()
{
    return settings().value("settings/outputDir", "").toString();
}

void Settings::setOutputDir(const QString &dir)
{
    settings().setValue("settings/outputDir", dir);
}

int Settings::maxParallel()
{
    return settings().value("settings/maxParallel", 0).toInt();
}

void Settings::setMaxParallel(int n)
{
    settings().setValue("settings/maxParallel", n);
}

QString Settings::defaultPresetName()
{
    return settings().value("settings/defaultPresetName", "").toString();
}

void Settings::setDefaultPresetName(const QString &name)
{
    settings().setValue("settings/defaultPresetName", name);
}

QString Settings::ffmpegDir()
{
    return settings().value("settings/ffmpegDir", "").toString();
}

void Settings::setFfmpegDir(const QString &dir)
{
    settings().setValue("settings/ffmpegDir", dir);
}

bool Settings::systemNotifications()
{
    return settings().value("settings/notifications", true).toBool();
}

void Settings::setSystemNotifications(bool on)
{
    settings().setValue("settings/notifications", on);
}

QString Settings::theme()
{
    return settings().value("settings/theme", "dark").toString();
}

void Settings::setTheme(const QString &name)
{
    settings().setValue("settings/theme", name);
}
