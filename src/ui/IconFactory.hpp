#pragma once

#include <QIcon>
#include <QString>

// 生成着色后的 SVG 图标：把资源中的 currentColor 按语义色替换后渲染成多尺寸 QIcon。
// 选用的颜色在深色与浅色主题下都有足够对比度。
namespace IconFactory {

QIcon icon(const QString &name); // 传文件名（不含 .svg），如 "play"

} // namespace IconFactory
