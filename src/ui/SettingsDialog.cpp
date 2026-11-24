#include "ui/SettingsDialog.hpp"
#include "ui_SettingsDialog.h"
#include "ui/IconFactory.hpp"
#include "presets/PresetManager.hpp"
#include "settings/Settings.hpp"
#include "settings/ThemeManager.hpp"

#include <QFileDialog>
#include <QPushButton>

SettingsDialog::SettingsDialog(QWidget *parent) : QDialog(parent), ui(new Ui::SettingsDialog)
{
    ui->setupUi(this);
    setWindowIcon(IconFactory::icon("settings"));

    ui->outputDirEdit->setText(Settings::outputDir());
    ui->outputBrowseBtn->setCursor(Qt::PointingHandCursor);

    ui->ffmpegDirEdit->setText(Settings::ffmpegDir());
    ui->ffmpegBrowseBtn->setCursor(Qt::PointingHandCursor);

    ui->maxParallelSpin->setValue(Settings::maxParallel());

    // itemData 存预设名；最后一项空串表示"未指定"
    for (const TranscodePreset &p : PresetManager::presets())
        ui->defaultPresetCombo->addItem(p.name, p.name);
    ui->defaultPresetCombo->addItem(tr("（未指定：使用列表第一个）"), QString());
    int idx = ui->defaultPresetCombo->findData(Settings::defaultPresetName());
    if (idx < 0)
        idx = ui->defaultPresetCombo->findData(QString()); // 已保存的预设被删除时回到"未指定"
    if (idx >= 0)
        ui->defaultPresetCombo->setCurrentIndex(idx);

    ui->notificationsCheck->setChecked(Settings::systemNotifications());

    ui->themeCombo->addItems(ThemeManager::themeNames());
    ui->themeCombo->setCurrentText(Settings::theme());

    ui->saveBtn->setProperty("accent", true);
    ui->saveBtn->setCursor(Qt::PointingHandCursor);
    ui->cancelBtn->setCursor(Qt::PointingHandCursor);

    connect(ui->saveBtn, &QPushButton::clicked, this, &SettingsDialog::onSave);
    connect(ui->cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    connect(ui->outputBrowseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseOutput);
    connect(ui->ffmpegBrowseBtn, &QPushButton::clicked, this, &SettingsDialog::onBrowseFfmpeg);
}

SettingsDialog::~SettingsDialog()
{
    delete ui;
}

void SettingsDialog::onBrowseOutput()
{
    QString dir =
        QFileDialog::getExistingDirectory(this, tr("选择默认输出目录"), ui->outputDirEdit->text());
    if (!dir.isEmpty())
        ui->outputDirEdit->setText(dir);
}

void SettingsDialog::onBrowseFfmpeg()
{
    QString dir = QFileDialog::getExistingDirectory(
        this, tr("选择 ffmpeg/ffprobe 所在目录"), ui->ffmpegDirEdit->text());
    if (!dir.isEmpty())
        ui->ffmpegDirEdit->setText(dir);
}

void SettingsDialog::onSave()
{
    Settings::setOutputDir(ui->outputDirEdit->text().trimmed());
    Settings::setFfmpegDir(ui->ffmpegDirEdit->text().trimmed());
    Settings::setMaxParallel(ui->maxParallelSpin->value());
    Settings::setDefaultPresetName(ui->defaultPresetCombo->currentData().toString());
    Settings::setSystemNotifications(ui->notificationsCheck->isChecked());
    if (ui->themeCombo->currentText() != ThemeManager::currentTheme())
        ThemeManager::setTheme(ui->themeCombo->currentText());
    accept();
}
