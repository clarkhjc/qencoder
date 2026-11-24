#pragma once

#include <QString>

// 任务实体：描述一次转码（步骤 4，先占位供 EnvironmentCheck 就绪后填充）
struct TranscodeTask
{
    QString id;            // 唯一标识（递增数字或 UUID）
    QString inputPath;     // 输入文件路径
    QString outputPath;    // 输出文件路径
    QString presetName;    // 关联的预设名（默认/自定义）
    QString presetJson;    // 预设快照（入队时序列化，之后预设改名/删除不影响本任务）

    // 运行状态
    enum class State { Waiting, Running, Paused, Done, Failed, Cancelled };
    State state = State::Waiting;

    double progress = 0.0;    // 0-100
};
