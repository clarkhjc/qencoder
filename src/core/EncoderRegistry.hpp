#pragma once

#include <QSet>
#include <QString>

// 预设保存时的编码器可用性校验（解析 ffmpeg -encoders，结果缓存）。
// 空串或 "copy" 视为合法（流拷贝）。无法获取列表时不阻塞用户。
namespace EncoderRegistry {

QSet<QString> encoders();
bool isKnown(const QString &codec); // 编码器名是否存在
void refresh();                     // 清空缓存（例如切换 ffmpeg 目录后）

} // namespace EncoderRegistry
