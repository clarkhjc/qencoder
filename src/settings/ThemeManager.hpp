#pragma once

#include <QString>
#include <QStringList>

// 步骤 7 补充：主题管理——三套 QSS（dark/light/pink），在设置里手动切换
class ThemeManager
{
public:
    static void initialize();                  // 读取设置并应用
    static QStringList themeNames();           // {"dark", "light", "pink"}
    static QString currentTheme();
    static void setTheme(const QString &name); // 保存设置 + 应用
    static void apply(const QString &name);    // 仅应用（不改设置）
    static QString themePath(const QString &name);
};
