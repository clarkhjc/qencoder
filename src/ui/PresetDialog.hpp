#pragma once

#include "presets/PresetManager.hpp"

#include <QDialog>

namespace Ui {
class PresetEditor;
class PresetDialog;
}

// 步骤 3：可视化参数预设——PresetDialog（预设列表）+ PresetEditor（编辑 + 生效参数预览表）

class PresetEditor : public QDialog
{
    Q_OBJECT
public:
    explicit PresetEditor(TranscodePreset p, QWidget *parent = nullptr);
    ~PresetEditor() override;

    TranscodePreset result() const { return m_preset; }

private slots:
    void updatePreview();

private:
    Ui::PresetEditor *ui;
    TranscodePreset m_preset;

    void setRow(int r, const QString &label, const QString &value);
};

class PresetDialog : public QDialog
{
    Q_OBJECT
public:
    explicit PresetDialog(QWidget *parent = nullptr);
    ~PresetDialog() override;

private slots:
    void onNew();
    void onEdit();
    void onClone();
    void onDelete();
    void onImport();
    void onExport();

private:
    void refreshList();

    Ui::PresetDialog *ui;
};
