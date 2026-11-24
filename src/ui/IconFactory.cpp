#include "ui/IconFactory.hpp"

#include <QFile>
#include <QHash>
#include <QImage>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>

namespace {

QString colorFor(const QString &name)
{
    if (name == "play")
        return QStringLiteral("#2ea043"); // 绿色：启动
    if (name == "pause")
        return QStringLiteral("#d29922"); // 琥珀：暂停
    if (name == "resume")
        return QStringLiteral("#4c8dff"); // 蓝色：恢复
    if (name == "stop" || name == "trash" || name == "cancel")
        return QStringLiteral("#f85149"); // 红色：停止/删除/取消
    if (name == "add-file" || name == "add-folder")
        return QStringLiteral("#4c8dff"); // 蓝色：添加
    return QStringLiteral("#8b949e");     // 灰色：设置/预设/取消
}

QIcon renderIcon(const QString &name)
{
    QFile f(QStringLiteral(":/icons/%1.svg").arg(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};

    QByteArray svg = f.readAll();
    svg.replace("currentColor", colorFor(name).toUtf8());

    QSvgRenderer renderer(svg);
    if (!renderer.isValid())
        return {};

    QIcon out;
    for (int size : {16, 20, 24, 32, 48, 64, 128}) {
        QImage image(size, size, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer.render(&painter, QRectF(0, 0, size, size));
        painter.end();
        out.addPixmap(QPixmap::fromImage(image));
    }
    return out;
}

} // namespace

QIcon IconFactory::icon(const QString &name)
{
    static QHash<QString, QIcon> cache;
    const auto it = cache.constFind(name);
    if (it != cache.constEnd())
        return it.value();
    const QIcon result = renderIcon(name);
    cache.insert(name, result);
    return result;
}
