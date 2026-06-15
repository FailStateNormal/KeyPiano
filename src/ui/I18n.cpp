#include "I18n.h"

namespace keypiano::ui {

Translator::Translator(QObject* parent) : QTranslator(parent) {
    // English source text -> Simplified Chinese. Keep %1/%2 placeholders intact.
    map_ = {
        // ── MainWindow: menus / actions / toolbar ───────────────────────────
        {"&File", QStringLiteral("文件(&F)")},
        {"Open SF&2...", QStringLiteral("打开 SF&2 音色库...")},
        {"Open &VST3 Instrument...", QStringLiteral("打开 &VST3 乐器插件...")},
        {"Show Plugin &Editor...", QStringLiteral("显示插件编辑器(&E)...")},
        {"Open &Keymap...", QStringLiteral("打开键位映射(&K)...")},
        {"&Edit Keymap Labels...", QStringLiteral("编辑键位标签(&E)...")},
        {"Re&bind Keys (click a key, then press)",
         QStringLiteral("重绑按键(&B)（先点琴键，再按物理键）")},
        {"Reset Keymap to &Default", QStringLiteral("重置键位为默认(&D)")},
        {"Key&map Presets", QStringLiteral("键位预设(&M)")},
        {"&Settings...", QStringLiteral("设置(&S)...")},
        {"E&xit", QStringLiteral("退出(&X)")},
        {"&Record", QStringLiteral("录制(&R)")},
        {"&Start Recording", QStringLiteral("开始录制(&S)")},
        {"&Stop", QStringLiteral("停止(&S)")},
        {"&Playback", QStringLiteral("回放(&P)")},
        {"Controls", QStringLiteral("控制")},
        {"&Help", QStringLiteral("帮助(&H)")},
        {"&Usage Guide...", QStringLiteral("使用说明(&U)...")},
        {"&About keypiano...", QStringLiteral("关于 keypiano(&A)...")},
        {"&Language", QStringLiteral("语言(&L)")},

        // ── MainWindow: dialogs / status messages ───────────────────────────
        {"Audio Error", QStringLiteral("音频错误")},
        {"Failed to open audio output device.\n"
         "SF2 playback will be unavailable.",
         QStringLiteral("无法打开音频输出设备。\n将无法播放 SF2 音色。")},
        {"Hook Error", QStringLiteral("键盘钩子错误")},
        {"Failed to install keyboard hook.\n"
         "Keyboard input will be unavailable.",
         QStringLiteral("无法安装键盘钩子。\n将无法使用键盘输入。")},
        {"Failed to open audio with the new settings.\n"
         "Reverting to defaults.",
         QStringLiteral("无法以新设置打开音频。\n已恢复为默认设置。")},
        {"Default Keymap", QStringLiteral("默认键位映射")},
        {"Bundled default keymap has issues:\n%1",
         QStringLiteral("内置默认键位映射存在问题：\n%1")},
        {" (built-in)", QStringLiteral("（内置）")},
        {"Error", QStringLiteral("错误")},
        {"Audio engine is not running.", QStringLiteral("音频引擎未在运行。")},
        {"Failed to load SoundFont:\n%1",
         QStringLiteral("加载音色库失败：\n%1")},
        {"Loaded: %1", QStringLiteral("已加载：%1")},
        {"Select VST3 instrument (.vst3 bundle folder)",
         QStringLiteral("选择 VST3 乐器（.vst3 文件夹）")},
        {"Failed to start audio with the VST3 backend.",
         QStringLiteral("无法以 VST3 后端启动音频。")},
        {"Failed to load VST3 instrument:\n%1\n\n"
         "Make sure the folder is a valid .vst3 bundle containing an "
         "instrument plug-in.",
         QStringLiteral("加载 VST3 乐器失败：\n%1\n\n"
                        "请确认所选文件夹是包含乐器插件的有效 .vst3 包。")},
        {"Loaded VST3: %1", QStringLiteral("已加载 VST3：%1")},
        {"Plugin Editor", QStringLiteral("插件编辑器")},
        {"The current backend has no plug-in editor.",
         QStringLiteral("当前后端没有插件编辑器。")},
        {"This plug-in does not provide a custom editor window.",
         QStringLiteral("该插件未提供自定义编辑器窗口。")},
        {"Keymap labels updated.", QStringLiteral("键位标签已更新。")},
        {"Rebind: click a piano key, then press the keyboard key to assign.",
         QStringLiteral("重绑：先点击一个琴键，再按下要绑定的键盘按键。")},
        {"Press a keyboard key to bind to %1...",
         QStringLiteral("按下键盘按键以绑定到 %1……")},
        {"Bound %1 -> %2", QStringLiteral("已绑定 %1 -> %2")},
        {"Reset Keymap", QStringLiteral("重置键位映射")},
        {"Discard your custom key bindings and restore the default layout?",
         QStringLiteral("放弃自定义键位绑定并恢复默认布局？")},
        {"Keymap reset to default.", QStringLiteral("键位已重置为默认。")},
        {"(no saved presets)", QStringLiteral("（暂无已保存预设）")},
        {"Save Current As...", QStringLiteral("将当前另存为...")},
        {"Delete Preset...", QStringLiteral("删除预设...")},
        {"Save Keymap Preset", QStringLiteral("保存键位预设")},
        {"Preset name:", QStringLiteral("预设名称：")},
        {"Overwrite Preset", QStringLiteral("覆盖预设")},
        {"A preset named \"%1\" already exists. Overwrite it?",
         QStringLiteral("已存在名为“%1”的预设，是否覆盖？")},
        {"Could not write preset:\n%1",
         QStringLiteral("无法写入预设：\n%1")},
        {"Saved preset: %1", QStringLiteral("已保存预设：%1")},
        {"Cannot read preset:\n%1", QStringLiteral("无法读取预设：\n%1")},
        {"Preset Warnings", QStringLiteral("预设警告")},
        {"Loaded preset: %1", QStringLiteral("已加载预设：%1")},
        {"Delete Preset", QStringLiteral("删除预设")},
        {"There are no saved presets.", QStringLiteral("没有已保存的预设。")},
        {"Preset to delete:", QStringLiteral("要删除的预设：")},
        {"Delete preset \"%1\"? This cannot be undone.",
         QStringLiteral("删除预设“%1”？此操作无法撤销。")},
        {"Deleted preset: %1", QStringLiteral("已删除预设：%1")},
        {"Audio settings applied.", QStringLiteral("音频设置已应用。")},
        {"Open Keymap", QStringLiteral("打开键位映射")},
        {"Keymap files (*.map);;All files (*)",
         QStringLiteral("键位映射文件 (*.map);;所有文件 (*)")},
        {"Cannot read keymap:\n%1", QStringLiteral("无法读取键位映射：\n%1")},
        {"Keymap Warnings", QStringLiteral("键位映射警告")},
        {"Loaded keymap: %1", QStringLiteral("已加载键位映射：%1")},
        {"Recording...", QStringLiteral("正在录制……")},
        {"Playback stopped.", QStringLiteral("回放已停止。")},
        {"Recording stopped — %1 events.",
         QStringLiteral("录制已停止 —— 共 %1 个事件。")},
        {"Save Recording", QStringLiteral("保存录制")},
        {"Save %1 event(s) to file?",
         QStringLiteral("将 %1 个事件保存到文件？")},
        {"keypiano performance (*.kps);;All files (*)",
         QStringLiteral("keypiano 演奏文件 (*.kps);;所有文件 (*)")},
        {"Save Error", QStringLiteral("保存错误")},
        {"Failed to save:\n%1", QStringLiteral("保存失败：\n%1")},
        {"Saved: %1", QStringLiteral("已保存：%1")},
        {"Playback", QStringLiteral("回放")},
        {"Nothing to play back.\nRecord something first.",
         QStringLiteral("没有可回放的内容。\n请先录制一段。")},
        {"Playing back %1 event(s)...",
         QStringLiteral("正在回放 %1 个事件……")},
        {"Latency: %1 ms", QStringLiteral("延迟：%1 毫秒")},
        {"CPU: %1%", QStringLiteral("CPU：%1%")},

        // ── SoundFontDialog ─────────────────────────────────────────────────
        {"Open SoundFont", QStringLiteral("打开音色库")},
        {"Recent files:", QStringLiteral("最近文件：")},
        {"Remove", QStringLiteral("移除")},
        {"Remove the selected entry from the recent list",
         QStringLiteral("从最近列表中移除选中项")},
        {"Clear All", QStringLiteral("全部清除")},
        {"Browse...", QStringLiteral("浏览...")},
        {"(none selected)", QStringLiteral("（未选择）")},
        {"SoundFont 2 (*.sf2);;All files (*)",
         QStringLiteral("SoundFont 2 (*.sf2);;所有文件 (*)")},

        // ── SettingsDialog ──────────────────────────────────────────────────
        {"Audio Settings", QStringLiteral("音频设置")},
        {"System Default", QStringLiteral("系统默认")},
        {"Changes take effect immediately (audio will briefly drop out).",
         QStringLiteral("更改将立即生效（音频会短暂中断）。")},
        {"Output Device:", QStringLiteral("输出设备：")},
        {"Sample Rate (Hz):", QStringLiteral("采样率 (Hz)：")},
        {"Buffer Frames:", QStringLiteral("缓冲帧数：")},

        // ── KeyMapEditorDialog ──────────────────────────────────────────────
        {"Keymap Editor — %1", QStringLiteral("键位映射编辑器 —— %1")},
        {"(unnamed)", QStringLiteral("（未命名）")},
        {"Double-click the Label cell to edit the overlay text shown on the "
         "piano.",
         QStringLiteral("双击“标签”单元格，可编辑显示在琴键上的覆盖文字。")},
        {"Label", QStringLiteral("标签")},
        {"VK Code", QStringLiteral("VK 码")},
        {"Action", QStringLiteral("动作")},
        {"Channel", QStringLiteral("通道")},
        {"MIDI Note", QStringLiteral("MIDI 音符")},
        {"Step", QStringLiteral("步进")},
    };
}

QString Translator::translate(const char* /*context*/, const char* sourceText,
                              const char* /*disambiguation*/, int /*n*/) const {
    const auto it = map_.constFind(QString::fromUtf8(sourceText));
    if (it != map_.constEnd()) return it.value();
    return QString();  // no entry -> Qt falls back to the source (English) text
}

}  // namespace keypiano::ui
