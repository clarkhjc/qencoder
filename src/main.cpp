#include "ui/MainWindow.hpp"
#include "core/EnvironmentCheck.hpp"
#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"
#include "settings/ThemeManager.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    QCoreApplication::setApplicationName("QEncoder");
    QCoreApplication::setOrganizationName("QEncoder");

    Settings::initialize();
    EnvironmentCheck::initialize();
    PresetManager::initialize();
    ThemeManager::initialize();

    MainWindow w;
    w.show();

    if (!EnvironmentCheck::isValid()) {
        QMessageBox::warning(
            &w, QObject::tr("缺少 ffmpeg"),
            QObject::tr("未找到 ffmpeg/ffprobe。\n"
                        "请在“设置 → ffmpeg 目录”中指定其所在目录，"
                        "或安装 ffmpeg 并将其加入 PATH 后重启。"));
    }

    return app.exec();
}
