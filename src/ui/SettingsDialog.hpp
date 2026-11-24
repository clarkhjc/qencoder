#pragma once

#include <QDialog>

namespace Ui {
class SettingsDialog;
}

// 步骤 7 补充：设置对话框——默认输出目录、ffmpeg 目录、并发数、默认预设、主题、系统通知
class SettingsDialog : public QDialog
{
    Q_OBJECT
public:
    explicit SettingsDialog(QWidget *parent = nullptr);
    ~SettingsDialog() override;

private slots:
    void onBrowseOutput();
    void onBrowseFfmpeg();
    void onSave();

private:
    Ui::SettingsDialog *ui;
};
