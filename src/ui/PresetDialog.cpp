#include <utility>
#include "ui/PresetDialog.hpp"
#include "ui_PresetDialog.h"
#include "ui_PresetEditor.h"
#include "core/EncoderRegistry.hpp"
#include "ui/IconFactory.hpp"
#include "core/MediaProbe.hpp"

#include <QAbstractItemView>
#include <QFile>
#include <QFileDialog>
#include <QFormLayout>
#include <QHeaderView>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>

namespace {

// 分辨率格式校验：留空 / "原始" / "1920x1080" / "原比例 1080P"
bool isValidSize(const QString &size)
{
    const QString s = size.trimmed();
    if (s.isEmpty() || s == "原始")
        return true;
    static const QRegularExpression fixed(QStringLiteral("^\\d+[xX]\\d+$"));
    static const QRegularExpression ratio(QStringLiteral("^原比例\\s*\\d+\\s*[pP]$"));
    return fixed.match(s).hasMatch() || ratio.match(s).hasMatch();
}

// 分辨率展示："原比例 N P" 语义 + 已探测的输入分辨率
QString effectiveSize(const TranscodePreset &p, const MediaInfo &info)
{
    if (p.size.isEmpty() || p.size == "原始") {
        if (info.ok && info.hasVideo)
            return QString("原分辨率 %1x%2").arg(info.width).arg(info.height);
        return QString::fromUtf8("原分辨率（保持）");
    }
    static const QRegularExpression ratio(QStringLiteral("^原比例\\s*(\\d+)\\s*[pP]$"));
    const QRegularExpressionMatch m = ratio.match(p.size.trimmed());
    if (m.hasMatch())
        return QString::fromUtf8("短边 %1P（按宽高比缩放）").arg(m.captured(1));
    return p.size;
}

} // namespace

// ---------------- PresetEditor：左侧参数表单 + 右侧生效参数预览表 ----------------

PresetEditor::PresetEditor(TranscodePreset p, QWidget *parent)
    : QDialog(parent), ui(new Ui::PresetEditor), m_preset(std::move(p))
{
    ui->setupUi(this);
    setWindowIcon(IconFactory::icon("sliders"));

    ui->formLayout->setLabelAlignment(Qt::AlignRight | Qt::AlignVCenter);
    ui->formLayout->setFormAlignment(Qt::AlignLeft | Qt::AlignTop);
    ui->formLayout->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    ui->paramsTitle->setObjectName("sectionTitle");
    ui->previewTitle->setObjectName("sectionTitle");

    ui->achannelsSpin->setValue(m_preset.achannels);

    // ---- 右：生效参数预览 ----
    ui->previewTable->setColumnCount(2);
    ui->previewTable->setHorizontalHeaderLabels(QStringList() << tr("参数") << tr("生效值"));
    ui->previewTable->verticalHeader()->setVisible(false);
    ui->previewTable->verticalHeader()->setDefaultSectionSize(30);
    ui->previewTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    ui->previewTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->previewTable->horizontalHeader()->setDefaultAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->previewTable->setAlternatingRowColors(true);
    ui->previewTable->setShowGrid(false);
    ui->previewTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->previewTable->setSelectionMode(QAbstractItemView::NoSelection);
    ui->previewTable->setFocusPolicy(Qt::NoFocus);

    // 把当前预设填进表单
    ui->nameEdit->setText(m_preset.name);
    ui->containerEdit->setText(m_preset.container);
    ui->vcodecEdit->setText(m_preset.vcodec);
    ui->acodecEdit->setText(m_preset.acodec);
    ui->vpresetEdit->setText(m_preset.vpreset);
    ui->crfEdit->setText(m_preset.crf);
    ui->vbitrateEdit->setText(m_preset.vbitrate);
    ui->sizeEdit->setText(m_preset.size);
    ui->fpsEdit->setText(m_preset.fps);
    ui->abitrateEdit->setText(m_preset.abitrate);
    ui->arateEdit->setText(m_preset.arate);
    ui->extraEdit->setText(m_preset.extraArgs);

    connect(ui->nameEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->containerEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->vcodecEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->acodecEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->vpresetEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->crfEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->vbitrateEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->sizeEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->fpsEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->abitrateEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->arateEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);
    connect(ui->achannelsSpin, &QSpinBox::valueChanged, this, &PresetEditor::updatePreview);
    connect(ui->extraEdit, &QLineEdit::textChanged, this, &PresetEditor::updatePreview);

    ui->okBtn->setProperty("accent", true);
    ui->okBtn->setCursor(Qt::PointingHandCursor);
    ui->cancelBtn->setCursor(Qt::PointingHandCursor);

    connect(ui->okBtn, &QPushButton::clicked, this, [this](bool) {
        updatePreview();
        if (ui->nameEdit->text().trimmed().isEmpty()) {
            QMessageBox::warning(this, tr("参数错误"), tr("预设名称不能为空"));
            return;
        }
        if (!isValidSize(ui->sizeEdit->text())) {
            QMessageBox::warning(
                this, tr("参数错误"),
                tr("分辨率格式无效：%1\n支持 1920x1080、原比例 1080P 或留空。")
                    .arg(ui->sizeEdit->text().trimmed()));
            return;
        }
        // 编码器可用性校验（缓存 ffmpeg -encoders 结果）
        QStringList missing;
        const QString vcodec = ui->vcodecEdit->text().trimmed();
        const QString acodec = ui->acodecEdit->text().trimmed();
        if (!EncoderRegistry::isKnown(vcodec))
            missing << tr("视频编码器 %1").arg(vcodec);
        if (!EncoderRegistry::isKnown(acodec))
            missing << tr("音频编码器 %1").arg(acodec);
        if (!missing.isEmpty()) {
            const auto ret = QMessageBox::warning(
                this, tr("编码器不存在"),
                tr("以下编码器在当前 ffmpeg 中未找到：\n%1\n\n仍要保存吗？")
                    .arg(missing.join('\n')),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
            if (ret != QMessageBox::Yes)
                return;
        }
        accept();
    });
    connect(ui->cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    updatePreview();
}

PresetEditor::~PresetEditor()
{
    delete ui;
}

void PresetEditor::setRow(int r, const QString &label, const QString &value)
{
    ui->previewTable->setRowCount(qMax(ui->previewTable->rowCount(), r + 1));
    auto *l = new QTableWidgetItem(label);
    l->setFlags(l->flags() & ~Qt::ItemIsEditable);
    l->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    auto *v = new QTableWidgetItem(value);
    v->setFlags(v->flags() & ~Qt::ItemIsEditable);
    v->setTextAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    ui->previewTable->setItem(r, 0, l);
    ui->previewTable->setItem(r, 1, v);
}

void PresetEditor::updatePreview()
{
    // 读取表单到预设实体
    m_preset.name = ui->nameEdit->text().trimmed();
    m_preset.container = ui->containerEdit->text().trimmed();
    m_preset.vcodec = ui->vcodecEdit->text().trimmed();
    m_preset.acodec = ui->acodecEdit->text().trimmed();
    m_preset.vpreset = ui->vpresetEdit->text().trimmed();
    m_preset.crf = ui->crfEdit->text().trimmed();
    m_preset.vbitrate = ui->vbitrateEdit->text().trimmed();
    m_preset.size = ui->sizeEdit->text().trimmed();
    m_preset.fps = ui->fpsEdit->text().trimmed();
    m_preset.abitrate = ui->abitrateEdit->text().trimmed();
    m_preset.arate = ui->arateEdit->text().trimmed();
    m_preset.achannels = ui->achannelsSpin->value();
    m_preset.extraArgs = ui->extraEdit->text().trimmed();

    MediaInfo info; // 编辑器内暂无输入文件
    int r = 0;
    auto set = [&](const QString &label, const QString &value) { setRow(r++, label, value); };

    const bool videoCopy = m_preset.vcodec.isEmpty()
                           || m_preset.vcodec.compare("copy", Qt::CaseInsensitive) == 0;
    const bool audioCopy = m_preset.acodec.isEmpty()
                           || m_preset.acodec.compare("copy", Qt::CaseInsensitive) == 0;

    ui->previewTable->setRowCount(0);
    set(tr("容器"), m_preset.container.isEmpty() ? tr("未指定（默认 mp4）") : m_preset.container);
    set(tr("视频编码"), videoCopy ? tr("拷贝（保持原编码器）") : m_preset.vcodec);
    set(tr("分辨率"), videoCopy ? tr("保持原样（视频拷贝时忽略）") : effectiveSize(m_preset, info));
    set(tr("帧率"), videoCopy ? tr("忽略（视频拷贝）")
                              : (m_preset.fps.isEmpty() ? tr("未设置") : m_preset.fps + " fps"));
    set(tr("-preset"), videoCopy ? tr("忽略（视频拷贝）")
                                 : (m_preset.vpreset.isEmpty() ? tr("未设置") : m_preset.vpreset));
    set(tr("-crf"), videoCopy ? tr("忽略（视频拷贝）")
                              : (m_preset.crf.isEmpty() ? tr("未设置") : m_preset.crf));
    set(tr("视频码率"), videoCopy ? tr("忽略（视频拷贝）")
                                  : (m_preset.vbitrate.isEmpty() ? tr("未设置") : m_preset.vbitrate));
    set(tr("音频编码"), audioCopy ? tr("拷贝（保持）") : m_preset.acodec);
    set(tr("音频码率"), audioCopy ? tr("忽略（音频拷贝）")
                                  : (m_preset.abitrate.isEmpty() ? tr("默认") : m_preset.abitrate));
    set(tr("采样率"), audioCopy ? tr("忽略（音频拷贝）")
                                : (m_preset.arate.isEmpty() ? tr("默认") : m_preset.arate));
    set(tr("声道数"), audioCopy ? tr("忽略（音频拷贝）")
                                : (ui->achannelsSpin->value() == 0
                                       ? tr("默认（保持原声道）")
                                       : QString::number(ui->achannelsSpin->value())));
    set(tr("追加参数"), m_preset.extraArgs.isEmpty() ? tr("无") : m_preset.extraArgs);
}

// ---------------- PresetDialog：预设管理 ----------------

PresetDialog::PresetDialog(QWidget *parent) : QDialog(parent), ui(new Ui::PresetDialog)
{
    ui->setupUi(this);
    setWindowIcon(IconFactory::icon("sliders"));

    ui->presetList->setSelectionMode(QAbstractItemView::SingleSelection);
    ui->presetList->setAlternatingRowColors(true);
    ui->deleteBtn->setProperty("danger", true);
    for (QPushButton *b : {ui->newBtn, ui->editBtn, ui->cloneBtn, ui->deleteBtn, ui->importBtn,
                           ui->exportBtn})
        b->setCursor(Qt::PointingHandCursor);

    refreshList();

    connect(ui->presetList, &QListWidget::itemDoubleClicked, this, [this] { onEdit(); });
    connect(ui->newBtn, &QPushButton::clicked, this, &PresetDialog::onNew);
    connect(ui->editBtn, &QPushButton::clicked, this, &PresetDialog::onEdit);
    connect(ui->cloneBtn, &QPushButton::clicked, this, &PresetDialog::onClone);
    connect(ui->deleteBtn, &QPushButton::clicked, this, &PresetDialog::onDelete);
    connect(ui->importBtn, &QPushButton::clicked, this, &PresetDialog::onImport);
    connect(ui->exportBtn, &QPushButton::clicked, this, &PresetDialog::onExport);
}

PresetDialog::~PresetDialog()
{
    delete ui;
}

void PresetDialog::refreshList()
{
    const int previous = ui->presetList->currentRow();
    ui->presetList->clear();
    for (const TranscodePreset &p : PresetManager::presets()) {
        auto *item = new QListWidgetItem(QString("%1  [%2/%3]")
                                             .arg(p.name,
                                                  p.vcodec.isEmpty() ? tr("拷贝") : p.vcodec,
                                                  p.container.isEmpty() ? "mp4" : p.container));
        ui->presetList->addItem(item);
    }
    if (PresetManager::presets().isEmpty())
        return;
    const int row = previous >= 0 ? qBound(0, previous, ui->presetList->count() - 1) : 0;
    ui->presetList->setCurrentRow(row);
}

void PresetDialog::onNew()
{
    PresetEditor editor(TranscodePreset(), this);
    if (editor.exec() != QDialog::Accepted)
        return;
    const TranscodePreset p = editor.result();
    if (p.name.isEmpty())
        return;
    if (PresetManager::hasName(p.name)) {
        QMessageBox::warning(this, tr("名称重复"),
                             tr("已存在同名预设：「%1」，请换一个名称。").arg(p.name));
        return;
    }
    PresetManager::addPreset(p);
    refreshList();
    ui->presetList->setCurrentRow(ui->presetList->count() - 1);
}

void PresetDialog::onEdit()
{
    const int row = ui->presetList->currentRow();
    if (row < 0 || row >= PresetManager::presets().size())
        return;
    PresetEditor editor(PresetManager::presets().at(row), this);
    if (editor.exec() != QDialog::Accepted)
        return;
    const TranscodePreset p = editor.result();
    if (p.name.isEmpty())
        return;
    if (PresetManager::hasName(p.name, row)) {
        QMessageBox::warning(this, tr("名称重复"),
                             tr("已存在同名预设：「%1」，请换一个名称。").arg(p.name));
        return;
    }
    PresetManager::updatePreset(row, p);
    refreshList();
    ui->presetList->setCurrentRow(row);
}

void PresetDialog::onClone()
{
    const int row = ui->presetList->currentRow();
    if (row < 0 || row >= PresetManager::presets().size())
        return;
    TranscodePreset p = PresetManager::presets().at(row);
    const QString base = p.name + tr("（副本）");
    QString name = base;
    for (int i = 2; PresetManager::hasName(name); ++i)
        name = QString("%1 %2").arg(base).arg(i);
    p.name = name;
    PresetManager::addPreset(p);
    refreshList();
    ui->presetList->setCurrentRow(ui->presetList->count() - 1);
}

void PresetDialog::onDelete()
{
    const int row = ui->presetList->currentRow();
    if (row < 0 || row >= PresetManager::presets().size())
        return;
    const QString name = PresetManager::presets().at(row).name;
    if (QMessageBox::question(this, tr("删除预设"), tr("确定删除预设「%1」？").arg(name))
        != QMessageBox::Yes)
        return;
    PresetManager::removePreset(row);
    refreshList();
}

void PresetDialog::onImport()
{
    QString file =
        QFileDialog::getOpenFileName(this, tr("导入预设"), QString(), tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导入失败"), tr("无法读取文件：%1").arg(file));
        return;
    }
    QString error;
    QList<TranscodePreset> list = PresetManager::fromJson(QString::fromUtf8(f.readAll()), &error);
    f.close();
    if (!error.isEmpty()) {
        QMessageBox::warning(this, tr("导入失败"), error);
        return;
    }

    int added = 0;
    int skipped = 0;
    for (const TranscodePreset &p : std::as_const(list)) {
        if (p.name.isEmpty() || PresetManager::hasName(p.name)) {
            ++skipped;
            continue;
        }
        PresetManager::addPreset(p);
        ++added;
    }
    refreshList();
    QMessageBox::information(this, tr("导入完成"),
                             tr("新增 %1 个预设，跳过 %2 个（名称为空或重名）。")
                                 .arg(added)
                                 .arg(skipped));
}

void PresetDialog::onExport()
{
    if (PresetManager::presets().isEmpty()) {
        QMessageBox::information(this, tr("导出预设"), tr("当前没有可导出的预设。"));
        return;
    }
    QString file =
        QFileDialog::getSaveFileName(this, tr("导出预设"), QString("qencoder_presets.json"),
                                     tr("JSON (*.json)"));
    if (file.isEmpty())
        return;
    QFile f(file);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QMessageBox::warning(this, tr("导出失败"), tr("无法写入文件：%1").arg(file));
        return;
    }
    const QByteArray data = PresetManager::toJson(PresetManager::presets()).toUtf8();
    if (f.write(data) != data.size() || !f.flush())
        QMessageBox::warning(this, tr("导出失败"), tr("写入文件失败：%1").arg(file));
}
