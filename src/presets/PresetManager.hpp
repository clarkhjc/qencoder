#pragma once

#include "core/TranscodePreset.hpp"

#include <QList>
#include <QString>

// 步骤 2：预设管理——内存列表 + QSettings 持久化 + JSON 导入导出
class PresetManager
{
public:
    static void initialize();

    static const QList<TranscodePreset> &presets();

    static void addPreset(const TranscodePreset &p);
    static void updatePreset(int index, const TranscodePreset &p);
    static void removePreset(int index);

    // 按名称查找；未找到返回空预设（name 为空）
    static TranscodePreset presetByName(const QString &name);

    // 新任务/目录批量添加时的默认预设：
    // 设置里的"默认预设" → 列表第一个 → 空预设
    static TranscodePreset defaultPreset();

    // 是否已存在同名预设（excludeIndex 用于编辑时排除自身）
    static bool hasName(const QString &name, int excludeIndex = -1);

    // JSON 导入导出（供分享/备份预设）
    static QString toJson(const QList<TranscodePreset> &presets);
    static QList<TranscodePreset> fromJson(const QString &json, QString *error = nullptr);

    // 单个预设的序列化（任务快照用）
    static QString presetToJson(const TranscodePreset &preset);
    static TranscodePreset presetFromJson(const QString &json);

private:
    static void load();
    static void save();
    static QList<TranscodePreset> &mutablePresets();
};
