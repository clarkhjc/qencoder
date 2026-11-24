#include <utility>
#include "ui/AddTasksDialog.hpp"
#include "ui_AddTasksDialog.h"
#include "ui/IconFactory.hpp"
#include "presets/PresetManager.hpp"

#include <QAbstractItemView>
#include <QDir>
#include <QDirIterator>
#include <QFileDialog>
#include <QFileInfo>
#include <QLabel>
#include <QMessageBox>
#include <QPushButton>
#include <QRegularExpression>
#include <QStackedWidget>

namespace {

// 支持的媒体扩展名（小写比较）
const QStringList &mediaExtensions()
{
    static const QStringList exts = QStringList()
                                    << "mp4" << "mkv" << "mov" << "avi" << "flv" << "webm"
                                    << "ts" << "m2ts" << "m4v" << "mpg" << "mpeg" << "wmv"
                                    << "ogv" << "3gp" << "mxf" << "m4a" << "mp3" << "flac"
                                    << "aac" << "ogg";
    return exts;
}

bool isMediaFile(const QString &fileName)
{
    return mediaExtensions().contains(QFileInfo(fileName).suffix().toLower());
}

QString mediaFilter()
{
    QStringList patterns;
    for (const QString &ext : mediaExtensions())
        patterns << "*." + ext;
    return QObject::tr("媒体文件") + " (" + patterns.join(' ') + ")";
}

QString containerExt(const QString &container)
{
    QString c = container.isEmpty() ? "mp4" : container;
    return c.remove('.').toLower();
}

// 预设名可能包含路径分隔符等非法文件名字符，拼接输出路径前先清洗
QString sanitizeFileName(QString name)
{
    name = name.trimmed();
    static const QRegularExpression invalid(QStringLiteral("[\\\\/:*?\"<>|\\x00-\\x1f]"));
    name.replace(invalid, "_");
    return name.isEmpty() ? QStringLiteral("default") : name;
}

} // namespace

AddTasksDialog::AddTasksDialog(TaskQueue *queue, QWidget *parent)
    : QDialog(parent), ui(new Ui::AddTasksDialog), m_queue(queue)
{
    ui->setupUi(this);
    setWindowIcon(IconFactory::icon("add-file"));

    // 顶部：预设选择卡片（itemData 存预设名，空串表示"默认自动"）
    ui->presetCard->setObjectName("card");
    ui->presetTitle->setObjectName("sectionTitle");
    for (const TranscodePreset &p : PresetManager::presets())
        ui->presetCombo->addItem(QString("%1 [%2]").arg(p.name, containerExt(p.container)),
                                 p.name);
    ui->presetCombo->addItem(tr("默认自动"), QString());
    const int defaultIdx = ui->presetCombo->findData(Settings::defaultPresetName());
    if (defaultIdx >= 0)
        ui->presetCombo->setCurrentIndex(defaultIdx);

    // 中部：已生成的任务预览列表与空状态
    ui->previewTitle->setObjectName("sectionTitle");
    ui->previewList->setAlternatingRowColors(true);
    ui->previewList->setSelectionMode(QAbstractItemView::NoSelection);
    ui->previewList->setFocusPolicy(Qt::NoFocus);
    ui->previewList->setMinimumHeight(180);
    ui->emptyIconLabel->setPixmap(IconFactory::icon("add-file").pixmap(48, 48));
    ui->previewStack->setCurrentWidget(ui->previewEmptyPage);

    ui->doneBtn->setProperty("accent", true);
    for (QPushButton *b : {ui->addFilesBtn, ui->addDirBtn, ui->doneBtn})
        b->setCursor(Qt::PointingHandCursor);

    connect(ui->addFilesBtn, &QPushButton::clicked, this, &AddTasksDialog::onAddFiles);
    connect(ui->addDirBtn, &QPushButton::clicked, this, &AddTasksDialog::onAddDir);
    connect(ui->doneBtn, &QPushButton::clicked, this, [this] { accept(); });

    updatePreviewState();
}

AddTasksDialog::~AddTasksDialog()
{
    delete ui;
}

void AddTasksDialog::accept()
{
    if (m_queue) {
        for (const TranscodeTask &t : std::as_const(m_pending))
            m_queue->addTask(t);
        m_pending.clear();
    }
    QDialog::accept();
}

void AddTasksDialog::promptForDirectory()
{
    onAddDir();
}

void AddTasksDialog::updatePreviewState()
{
    const int count = ui->previewList->count();
    ui->previewStack->setCurrentWidget(count > 0 ? static_cast<QWidget *>(ui->previewListPage)
                                                : ui->previewEmptyPage);
    ui->previewTitle->setText(count > 0 ? tr("任务预览（%1）").arg(count) : tr("任务预览"));
    ui->doneBtn->setText(count > 0 ? tr("关闭（任务入队）") : tr("关闭"));
}

void AddTasksDialog::onAddFiles()
{
    QStringList files =
        QFileDialog::getOpenFileNames(this, tr("选择媒体文件"), QDir::homePath(), mediaFilter());
    if (files.isEmpty())
        return;

    const QString settingsDir = Settings::outputDir().trimmed();
    const TranscodePreset preset = currentPreset();
    const QString presetName = currentPresetName();

    for (const QString &f : files) {
        if (!isMediaFile(f))
            continue;
        // 未设置默认输出目录时，写到各自输入文件所在目录（多选不同目录也能正确落盘）
        const QString outDir =
            settingsDir.isEmpty() ? QFileInfo(f).absolutePath() : settingsDir;
        if (!QDir().mkpath(outDir)) {
            QMessageBox::warning(this, tr("无法创建输出目录"), outDir);
            continue;
        }
        TranscodeTask t;
        t.inputPath = f;
        t.presetName = presetName;
        t.outputPath = uniqueOutputPath(makeOutputPath(f, preset, outDir));
        m_pending.append(t);
        ui->previewList->addItem(QString("%1  ->  %2").arg(QFileInfo(f).fileName(), t.outputPath));
    }
    updatePreviewState();
}

void AddTasksDialog::onAddDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, tr("选择媒体目录"), QDir::homePath());
    if (dir.isEmpty())
        return;
    QStringList files;
    const int count = scanDir(dir, files);
    if (files.isEmpty()) {
        QMessageBox::information(this, tr("提示"),
                                 tr("目录内没有可转码的媒体文件。（共扫描 %1 个文件）").arg(count));
        return;
    }

    // 输出目录：设置里配置的默认输出目录，或回退到输入目录
    QString outDir = Settings::outputDir().trimmed();
    if (outDir.isEmpty())
        outDir = dir;
    QDir outDirObj(outDir);
    outDir = outDirObj.absolutePath();
    if (!outDirObj.mkpath(".")) {
        QMessageBox::warning(this, tr("无法创建输出目录"), outDir);
        return;
    }

    const TranscodePreset preset = currentPreset();
    const QString presetName = currentPresetName();

    for (const QString &f : files) {
        TranscodeTask t;
        t.inputPath = f;
        t.presetName = presetName;
        t.outputPath = uniqueOutputPath(makeOutputPath(f, preset, outDir));
        m_pending.append(t);
        ui->previewList->addItem(QString("%1  ->  %2").arg(QFileInfo(f).fileName(), t.outputPath));
    }
    updatePreviewState();
}

TranscodePreset AddTasksDialog::currentPreset() const
{
    const QString name = ui->presetCombo->currentData().toString();
    if (!name.isEmpty()) {
        TranscodePreset p = PresetManager::presetByName(name);
        if (!p.name.isEmpty())
            return p;
    }
    return PresetManager::defaultPreset();
}

QString AddTasksDialog::currentPresetName() const
{
    // 空串表示"默认自动"：启动时再按设置解析默认预设
    const QString name = ui->presetCombo->currentData().toString();
    if (name.isEmpty())
        return {};
    const TranscodePreset p = PresetManager::presetByName(name);
    return p.name.isEmpty() ? QString() : p.name;
}

QString AddTasksDialog::uniqueOutputPath(const QString &candidate)
{
    const QFileInfo fi(candidate);
    const QString dir = fi.absolutePath();
    const QString fileName = fi.fileName();
    const int dot = fileName.lastIndexOf('.');
    const QString base = dot > 0 ? fileName.left(dot) : fileName;
    const QString suffix = dot > 0 ? fileName.mid(dot + 1) : QString();

    auto taken = [this](const QString &path) {
        if (m_plannedOutputs.contains(path) || QFileInfo::exists(path))
            return true;
        if (m_queue) {
            for (const TranscodeTask &t : m_queue->tasks())
                if (QFileInfo(t.outputPath).absoluteFilePath() == path)
                    return true;
        }
        return false;
    };

    QString path = fi.absoluteFilePath();
    for (int i = 1; taken(path); ++i) {
        const QString name = suffix.isEmpty()
                                 ? QString("%1_%2").arg(base).arg(i)
                                 : QString("%1_%2.%3").arg(base).arg(i).arg(suffix);
        path = QDir(dir).filePath(name);
    }
    m_plannedOutputs.insert(path);
    return path;
}

QString AddTasksDialog::makeOutputPath(const QString &inputPath, const TranscodePreset &preset,
                                       const QString &outDir)
{
    QString name = QFileInfo(inputPath).baseName();
    const QString base = sanitizeFileName(preset.name);
    return QDir(outDir).filePath(
        QString("%1_%2.%3").arg(name, base, containerExt(preset.container)));
}

int AddTasksDialog::scanDir(const QString &dirPath, QStringList &outFiles)
{
    int count = 0;
    // 递归扫描：目录下所有媒体文件（含子目录）
    QDirIterator it(dirPath, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString f = it.next();
        ++count;
        if (isMediaFile(f) && !outFiles.contains(f))
            outFiles.append(f);
    }
    return count;
}
