#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"

#include <QCoreApplication>
#include <QDebug>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>

namespace {

QList<TranscodePreset> builtInPresets()
{
    TranscodePreset h264;
    h264.name = "H.264 1080P";
    h264.container = "mp4";
    h264.vcodec = "libx264";
    h264.vpreset = "medium";
    h264.crf = "23";
    h264.size = "原比例 1080P";
    h264.acodec = "aac";
    h264.abitrate = "128k";
    h264.description = "常用 MP4 预设";
    return QList<TranscodePreset>() << h264;
}

} // namespace

namespace {

QJsonObject presetObject(const TranscodePreset &p)
{
    QJsonObject o;
    o["name"] = p.name;
    o["container"] = p.container;
    o["vcodec"] = p.vcodec;
    o["acodec"] = p.acodec;
    o["vpreset"] = p.vpreset;
    o["crf"] = p.crf;
    o["vbitrate"] = p.vbitrate;
    o["size"] = p.size;
    o["fps"] = p.fps;
    o["abitrate"] = p.abitrate;
    o["arate"] = p.arate;
    o["achannels"] = p.achannels;
    o["extraArgs"] = p.extraArgs;
    o["description"] = p.description;
    return o;
}

TranscodePreset presetFromObject(const QJsonObject &o)
{
    TranscodePreset p;
    p.name = o.value("name").toString().trimmed();
    p.container = o.value("container").toString();
    p.vcodec = o.value("vcodec").toString();
    p.acodec = o.value("acodec").toString();
    p.vpreset = o.value("vpreset").toString();
    p.crf = o.value("crf").toString();
    p.vbitrate = o.value("vbitrate").toString();
    p.size = o.value("size").toString();
    p.fps = o.value("fps").toString();
    p.abitrate = o.value("abitrate").toString();
    p.arate = o.value("arate").toString();
    p.achannels = o.value("achannels").toInt();
    p.extraArgs = o.value("extraArgs").toString();
    p.description = o.value("description").toString();
    return p;
}

} // namespace

QList<TranscodePreset> &PresetManager::mutablePresets()
{
    static QList<TranscodePreset> instance;
    return instance;
}

const QList<TranscodePreset> &PresetManager::presets()
{
    return mutablePresets();
}

void PresetManager::load()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    QString json = settings.value("presets/list").toString();
    if (json.isEmpty()) {
        // 首次运行：灌入内置常用预设
        mutablePresets() = builtInPresets();
        save();
        return;
    }

    QString error;
    QList<TranscodePreset> list = fromJson(json, &error);
    if (!error.isEmpty()) {
        // 数据损坏时回退到内置预设，避免所有任务退化为无参数转码
        qWarning() << "预设加载失败，回退到内置预设:" << error;
        mutablePresets() = builtInPresets();
        save();
        return;
    }
    mutablePresets() = list;
}

void PresetManager::save()
{
    QSettings settings(QCoreApplication::organizationName(), QCoreApplication::applicationName());
    settings.setValue("presets/list", toJson(presets()));
}

void PresetManager::initialize()
{
    load();
}

TranscodePreset PresetManager::presetByName(const QString &name)
{
    if (name.trimmed().isEmpty())
        return {};
    for (const TranscodePreset &p : presets())
        if (p.name.compare(name.trimmed(), Qt::CaseInsensitive) == 0)
            return p;
    return {};
}

TranscodePreset PresetManager::defaultPreset()
{
    const QString preferred = Settings::defaultPresetName();
    if (!preferred.isEmpty()) {
        TranscodePreset p = presetByName(preferred);
        if (!p.name.isEmpty())
            return p;
    }
    if (presets().isEmpty())
        return TranscodePreset();
    return presets().first();
}

bool PresetManager::hasName(const QString &name, int excludeIndex)
{
    const QString n = name.trimmed();
    if (n.isEmpty())
        return false;
    for (int i = 0; i < presets().size(); ++i)
        if (i != excludeIndex && presets().at(i).name.compare(n, Qt::CaseInsensitive) == 0)
            return true;
    return false;
}

void PresetManager::addPreset(const TranscodePreset &p)
{
    mutablePresets().append(p);
    save();
}

void PresetManager::updatePreset(int index, const TranscodePreset &p)
{
    if (index < 0 || index >= presets().size())
        return;
    mutablePresets()[index] = p;
    save();
}

void PresetManager::removePreset(int index)
{
    if (index < 0 || index >= presets().size())
        return;
    mutablePresets().removeAt(index);
    save();
}

QString PresetManager::toJson(const QList<TranscodePreset> &list)
{
    QJsonArray arr;
    for (const TranscodePreset &p : list)
        arr.append(presetObject(p));
    QJsonDocument doc(arr);
    return QString::fromUtf8(doc.toJson(QJsonDocument::Indented));
}

QString PresetManager::presetToJson(const TranscodePreset &preset)
{
    const QJsonDocument doc(presetObject(preset));
    return QString::fromUtf8(doc.toJson(QJsonDocument::Compact));
}

TranscodePreset PresetManager::presetFromJson(const QString &json)
{
    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    return presetFromObject(doc.object());
}

QList<TranscodePreset> PresetManager::fromJson(const QString &json, QString *error)
{
    QList<TranscodePreset> out;
    QJsonParseError err;
    QJsonDocument doc = QJsonDocument::fromJson(json.toUtf8(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isArray()) {
        if (error)
            *error = err.errorString();
        return out;
    }
    for (const QJsonValue &v : doc.array()) {
        TranscodePreset p = presetFromObject(v.toObject());
        if (p.name.isEmpty())
            continue; // 无名预设无法按名称引用，直接跳过
        out.append(p);
    }
    if (error)
        error->clear();
    return out;
}
