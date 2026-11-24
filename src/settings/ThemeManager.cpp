#include "settings/ThemeManager.hpp"
#include "settings/Settings.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSettings>
#include <QStyleHints>

QStringList ThemeManager::themeNames()
{
    return QStringList() << "dark" << "light" << "pink";
}

QString ThemeManager::currentTheme()
{
    return Settings::theme();
}

QString ThemeManager::themePath(const QString &name)
{
    // 1. macOS .app：Contents/MacOS/../Resources/themes
    QString p = QCoreApplication::applicationDirPath() + "/../Resources/themes/" + name + ".qss";
    if (QFileInfo::exists(p))
        return QFileInfo(p).absoluteFilePath();
    // 2. 应用目录（exe 同级 themes/）
    p = QCoreApplication::applicationDirPath() + "/themes/" + name + ".qss";
    if (QFileInfo::exists(p))
        return p;
    // 3. 当前工作目录（便于开发环境运行）
    p = QDir::currentPath() + "/themes/" + name + ".qss";
    if (QFileInfo::exists(p))
        return p;
    // 4. 源码根目录（CMake 工程时传入的路径）
    const QString envDir = qEnvironmentVariable("QENCODER_THEMES_DIR").trimmed();
    if (!envDir.isEmpty()) {
        p = QDir(envDir).filePath(name + ".qss");
        if (QFileInfo::exists(p))
            return p;
    }
    return {};
}

void ThemeManager::apply(const QString &name)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 8, 0)
    // 让原生标题栏/系统控件（如文件选择面板、微调按钮）跟随主题明暗
    if (auto *gui = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        gui->styleHints()->setColorScheme(name == "dark" ? Qt::ColorScheme::Dark
                                                          : Qt::ColorScheme::Light);
    }
#endif

    QString p = themePath(name);
    if (p.isEmpty()) {
        qWarning() << "未找到主题:" << name;
        return;
    }
    QFile f(p);
    if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString css = QString::fromUtf8(f.readAll());
        if (auto *app = qobject_cast<QApplication *>(QCoreApplication::instance()))
            app->setStyleSheet(css);
    } else {
        qWarning() << "读取主题失败:" << p;
    }
}

void ThemeManager::setTheme(const QString &name)
{
    if (!themeNames().contains(name))
        return;
    Settings::setTheme(name);
    apply(name);
}

void ThemeManager::initialize()
{
    QString t = Settings::theme();
    if (themeNames().contains(t))
        apply(t);
}
