#include "TranslationManager.h"

#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QEvent>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPointer>
#include <QSettings>
#include <QTabWidget>
#include <QTimer>
#include <QWidget>

#include <QHash>

namespace prs {

namespace {

const QHash<QString, QString>& chineseDictionary() {
    static const QHash<QString, QString> dictionary = {
        {"&File", "文件(&F)"}, {"&Edit", "编辑(&E)"}, {"&View", "视图(&V)"},
        {"&Settings", "设置(&S)"}, {"&New Project", "新建项目(&N)"},
        {"&Open Project...", "打开项目(&O)…"}, {"&Save Project", "保存项目(&S)"},
        {"Save Project &As...", "项目另存为(&A)…"}, {"Recent Projects", "最近的项目"},
        {"Clear Recent", "清除最近记录"}, {"(empty)", "（空）"}, {"E&xit", "退出(&X)"},
        {"&Undo", "撤销(&U)"}, {"&Redo", "重做(&R)"}, {"Cu&t", "剪切(&T)"},
        {"&Copy", "复制(&C)"}, {"&Paste", "粘贴(&P)"}, {"&Delete", "删除(&D)"},
        {"Select &All", "全选(&A)"}, {"Simulation &Queue", "仿真队列(&Q)"},
        {"&Preferences...", "偏好设置(&P)…"}, {"&Display Settings...", "显示设置(&D)…"},
        {"&Audio Settings...", "音频设置(&A)…"}, {"Si&mulation Settings...", "仿真设置(&M)…"},
        {"&Keyboard Shortcuts...", "键盘快捷键(&K)…"}, {"Language", "语言"},
        {"English", "English"}, {"Simplified Chinese", "简体中文"},
        {"Tools", "工具"}, {"Move", "移动"}, {"Copy", "复制"}, {"Cut", "剪切"},
        {"Paste", "粘贴"}, {"Delete", "删除"}, {"Measure", "测量"},
        {"Drag placed points to new positions", "拖动已放置的点以调整位置"},
        {"LIBRARY", "资源库"}, {"SOUND", "声音"}, {"MATERIAL", "材质"},
        {"No folder selected", "尚未选择文件夹"}, {"Browse Folder...", "浏览文件夹…"},
        {"Double-click to select a sound file", "双击选择声音文件"},
        {"ASSETS", "对象"}, {"SURFACES", "表面"}, {"SOURCES", "声源"},
        {"LISTENERS", "接收点"}, {"Click to select a source", "单击选择声源"},
        {"Click to select a listener", "单击选择接收点"}, {"PROPERTY", "属性"},
        {"Room Scale", "房间比例"}, {"Size: -- m", "尺寸：-- m"},
        {"Point Distance", "点距离"}, {"Selected Point", "已选点"},
        {"Selected Surface", "已选表面"}, {"Source", "声源"}, {"Listener", "接收点"},
        {"Volume:", "音量："}, {"Set Audio File", "设置音频文件"},
        {"No audio file", "没有音频文件"}, {"Facing Direction:", "朝向："},
        {"Deselect", "取消选择"}, {"Material: (none)", "材质：（无）"},
        {"Texture", "纹理"}, {"Load Texture...", "加载纹理…"},
        {"Import Room", "导入房间"}, {"Add Source", "添加声源"},
        {"Add Listener", "添加接收点"}, {"Render", "开始仿真"},
        {"Ready", "就绪"}, {"Auto-saved", "已自动保存"}, {"Cancelled", "已取消"},
        {"Error", "错误"}, {"Unsaved Changes", "未保存的更改"},
        {"Save current project before creating a new one?", "创建新项目之前是否保存当前项目？"},
        {"Save project before closing?", "关闭之前是否保存项目？"},
        {"Open Mesh Detected", "检测到开放网格"},
        {"Acoustic simulation results may be inaccurate for open meshes.", "开放网格可能导致声学仿真结果不准确。"},
        {"Simulation Complete", "仿真完成"}, {"Simulation Failed", "仿真失败"},
        {"No 3D model loaded.", "尚未加载三维模型。"},
        {"No sound sources placed.\nPlace a point and set its type to Source.", "尚未放置声源。\n请放置一个点并将其类型设为声源。"},
        {"No listeners placed.\nPlace a point and set its type to Listener.", "尚未放置接收点。\n请放置一个点并将其类型设为接收点。"},
        {"No listeners selected for rendering.", "尚未选择需要仿真的接收点。"},
        {"Failed to load texture image.", "纹理图像加载失败。"},
        {"Failed to save project.", "项目保存失败。"},
        {"Load a 3D model first", "请先加载三维模型"},
        {"Import Room Model", "导入房间模型"}, {"Open Project", "打开项目"},
        {"Save Project As", "项目另存为"}, {"Select Sound Source", "选择声源音频"},
        {"Select Audio File", "选择音频文件"}, {"Load Texture Image", "加载纹理图像"},
        {"Simulation Queue", "仿真队列"}, {"Cancel Current", "取消当前任务"},
        {"Cancel All", "全部取消"}, {"No simulations running", "没有正在运行的仿真"},
        {"Queue idle", "队列空闲"}, {"Double-click to view results and metrics", "双击查看结果和声学指标"},
        {"[Queued]", "[排队中]"}, {"[Done]", "[已完成]"}, {"[Failed]", "[失败]"},
        {"[Cancelled]", "[已取消]"}, {"Preferences", "偏好设置"},
        {"Default Project Dir:", "默认项目目录："}, {"Browse...", "浏览…"},
        {"Auto-save Interval:", "自动保存间隔："}, {"Off", "关闭"},
        {"Display Settings", "显示设置"}, {"Show measurement grid", "显示测量网格"},
        {"Grid minor spacing:", "网格小格间距："}, {"Point marker size:", "点标记大小："},
        {"Transparency alpha:", "透明度："}, {"Solid colors only (ignore surface textures)", "仅使用纯色（忽略表面纹理）"},
        {"Reset Surface Colors", "重置表面颜色"}, {"Audio Settings", "音频设置"},
        {"Sample Rate:", "采样率："}, {"Output Format:", "输出格式："},
        {"Simulation Settings", "仿真设置"}, {"Number of Rays:", "射线数量："},
        {"Max ISM Order:", "最大镜像声源阶数："}, {"Default Absorption:", "默认吸声系数："},
        {"Default Scattering:", "默认散射系数："}, {"Enable air absorption", "启用空气吸声"},
        {"Keyboard Shortcuts", "键盘快捷键"}, {"Action", "操作"},
        {"Shortcut", "快捷键"}, {"Close", "关闭"}, {"New Project", "新建项目"},
        {"Save Project", "保存项目"}, {"Save As", "另存为"}, {"Exit", "退出"},
        {"Undo", "撤销"}, {"Redo", "重做"}, {"Copy Point", "复制点"},
        {"Cut Point", "剪切点"}, {"Paste Point", "粘贴点"}, {"Delete Point", "删除点"},
        {"Clear All Points", "清除所有点"}, {"Toggle Transparency", "切换透明显示"},
        {"Render Options", "仿真选项"}, {"Select listeners to render:", "选择需要仿真的接收点："},
        {"Select All", "全选"}, {"Deselect All", "全不选"}, {"Cancel", "取消"},
        {"Simulation Method", "仿真方法"}, {"Method:", "方法："},
        {"Ray Tracing (ISM + Monte Carlo)", "射线追踪（镜像声源法 + 蒙特卡洛）"},
        {"DG Wave Solver (2D)", "DG 波动求解器（二维）"}, {"DG Wave Solver (3D)", "DG 波动求解器（三维）"},
        {"Polynomial order:", "多项式阶数："}, {"Max frequency:", "最高频率："},
        {"Compare Simulation Audio", "比较仿真音频"}, {"No file playing", "当前未播放文件"},
        {"Playback", "播放"}, {"Stop", "停止"}, {"Play A", "播放 A"},
        {"Play B", "播放 B"}, {"Toggle A/B", "切换 A/B"},
        {"Simulation Output Files", "仿真输出文件"}, {"Acoustic Metrics", "声学指标"},
        {"Total (dB)", "总声压级 (dB)"}, {"Direct (dB)", "直达声 (dB)"},
        {"Reflected (dB)", "反射声 (dB)"}, {"Stopped", "已停止"}
    };
    return dictionary;
}

QString original(QObject* object, const char* key, const QString& current) {
    const QByteArray propertyName = QByteArray("seicheOriginal_") + key;
    const QVariant saved = object->property(propertyName.constData());
    if (saved.isValid()) return saved.toString();
    object->setProperty(propertyName.constData(), current);
    return current;
}

} // namespace

TranslationManager& TranslationManager::instance() {
    static TranslationManager manager;
    return manager;
}

TranslationManager::TranslationManager() = default;

void TranslationManager::initialize() {
    QSettings settings;
    language_ = settings.value(QStringLiteral("ui/language"), QStringLiteral("zh_CN")).toString();
    qApp->installEventFilter(this);
}

void TranslationManager::setLanguage(const QString& language) {
    if (language_ == language) return;
    language_ = language;
    QSettings().setValue(QStringLiteral("ui/language"), language_);
    translateApplication();
    emit languageChanged(language_);
}

QString TranslationManager::translate(const QString& source) const {
    if (!isChinese() || source.isEmpty()) return source;
    const auto& dictionary = chineseDictionary();
    const auto exact = dictionary.constFind(source);
    if (exact != dictionary.cend()) return *exact;

    QString result = source;
    const QList<QPair<QString, QString>> prefixes = {
        {"Size: ", "尺寸："}, {"Material: ", "材质："}, {"Source ", "声源 "},
        {"Listener ", "接收点 "}, {"Surface ", "表面 "}, {"Distance: ", "距离："},
        {"Loaded: ", "已加载："}, {"Saved: ", "已保存："}, {"Project opened: ", "已打开项目："},
        {"Sound loaded: ", "已加载声音："}, {"Texture loaded: ", "已加载纹理："},
        {"Playing: ", "正在播放："}, {"Failed to load 3D model: ", "三维模型加载失败："},
        {"Failed to load project: ", "项目加载失败："}, {"Simulation queued: ", "仿真已加入队列："},
        {"Simulation complete: ", "仿真完成："}
    };
    for (const auto& pair : prefixes) {
        if (result.startsWith(pair.first)) {
            result.replace(0, pair.first.size(), pair.second);
            break;
        }
    }
    return result;
}

void TranslationManager::translateAction(QAction* action) {
    if (!action) return;
    action->setText(isChinese() ? translate(original(action, "text", action->text()))
                                : original(action, "text", action->text()));
    action->setToolTip(isChinese() ? translate(original(action, "toolTip", action->toolTip()))
                                   : original(action, "toolTip", action->toolTip()));
}

void TranslationManager::translateObject(QObject* object) {
    if (!object) return;
    if (auto* widget = qobject_cast<QWidget*>(object)) {
        const QString base = original(widget, "windowTitle", widget->windowTitle());
        widget->setWindowTitle(isChinese() ? translate(base) : base);
    }
    if (auto* label = qobject_cast<QLabel*>(object)) {
        const QString base = original(label, "text", label->text());
        label->setText(isChinese() ? translate(base) : base);
    }
    if (auto* button = qobject_cast<QAbstractButton*>(object)) {
        const QString base = original(button, "text", button->text());
        button->setText(isChinese() ? translate(base) : base);
    }
    if (auto* box = qobject_cast<QGroupBox*>(object)) {
        const QString base = original(box, "title", box->title());
        box->setTitle(isChinese() ? translate(base) : base);
    }
    if (auto* edit = qobject_cast<QLineEdit*>(object)) {
        const QString base = original(edit, "placeholder", edit->placeholderText());
        edit->setPlaceholderText(isChinese() ? translate(base) : base);
    }
    if (auto* tabs = qobject_cast<QTabWidget*>(object)) {
        for (int i = 0; i < tabs->count(); ++i) {
            const QByteArray key = QByteArray("tab_") + QByteArray::number(i);
            const QString base = original(tabs, key.constData(), tabs->tabText(i));
            tabs->setTabText(i, isChinese() ? translate(base) : base);
        }
    }
    if (auto* combo = qobject_cast<QComboBox*>(object)) {
        for (int i = 0; i < combo->count(); ++i) {
            const QByteArray key = QByteArray("item_") + QByteArray::number(i);
            const QString base = original(combo, key.constData(), combo->itemText(i));
            combo->setItemText(i, isChinese() ? translate(base) : base);
        }
    }
    const auto actions = object->findChildren<QAction*>(QString(), Qt::FindDirectChildrenOnly);
    for (QAction* action : actions) translateAction(action);
    const auto children = object->children();
    for (QObject* child : children) {
        if (!qobject_cast<QAction*>(child)) translateObject(child);
    }
}

void TranslationManager::translateApplication() {
    const auto widgets = QApplication::topLevelWidgets();
    for (QWidget* widget : widgets) translateObject(widget);
}

bool TranslationManager::eventFilter(QObject* watched, QEvent* event) {
    if (event->type() == QEvent::Show || event->type() == QEvent::ChildAdded) {
        const QPointer<QObject> safeObject(watched);
        QTimer::singleShot(0, this, [this, safeObject]() {
            if (safeObject) translateObject(safeObject);
        });
    }
    return QObject::eventFilter(watched, event);
}

} // namespace prs
