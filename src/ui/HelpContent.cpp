#include "HelpContent.h"

namespace keypiano::ui {

QString usageGuideHtmlEn() {
    return QStringLiteral(R"(
<h2>keypiano — Usage Guide</h2>

<h3>1. Getting started</h3>
<p>keypiano turns your computer keyboard into a piano. Just start the program and
press keys — it ships with a built-in piano sound, so it makes sound right away.</p>
<ul>
<li>The two rows of keys play two octaves. By default the <b>Z</b> row is the left
hand (lower) and the <b>Q</b> row is the right hand (higher).</li>
<li>You can also click the on-screen piano keys with the mouse.</li>
<li>Pedals work like a real piano — <b>hold</b> the key down: <b>Space</b> =
sustain (damper), <b>F3</b> = sostenuto, <b>F4</b> = soft (una corda).
<b>F1</b>/<b>F2</b> shift the octave down/up.</li>
</ul>

<h3>2. Changing the sound</h3>
<p>Two ways to change the instrument:</p>
<ul>
<li><b>File &rarr; Open SF2...</b> — load a SoundFont (<code>.sf2</code>) file. Free
GM SoundFonts such as <i>GeneralUser GS</i>, <i>FluidR3</i> or
<i>MuseScore_General</i> work well.</li>
<li><b>File &rarr; Open VST3 Instrument...</b> — load a VST3 plug-in (see below).</li>
</ul>

<h3>3. Using VST3 plug-ins</h3>
<p><b>keypiano does not include any plug-in.</b> You download and install one
yourself, then point keypiano at it.</p>
<ol>
<li>Download a free VST3 instrument — for example <b>Surge XT</b>
(<a href="https://surge-synthesizer.github.io/">surge-synthesizer.github.io</a>)
or <b>Vital</b>, <b>Dexed</b>, etc. Run its installer.</li>
<li>On Windows, VST3 plug-ins install into
<code>C:\Program Files\Common Files\VST3</code> as a <code>.vst3</code> folder.</li>
<li>In keypiano choose <b>File &rarr; Open VST3 Instrument...</b> and select that
<code>.vst3</code> folder. The dialog opens in the standard VST3 location.</li>
<li>Choose a sound inside the plug-in with <b>File &rarr; Show Plugin Editor...</b>
(Ctrl+E). This opens the plug-in's own window — use its patch/preset browser to
pick, for example, a piano. (Surge: open the patch browser at the top and pick a
"Keys"/piano patch.)</li>
</ol>
<p><b>Note:</b> the pedals (sustain/sostenuto/soft) and other MIDI CC currently
work only with the built-in SF2 engine. With a VST3 plug-in they are not yet
sent — use the SF2 engine if you need pedals.</p>
<p>To go back to the built-in sound, just use <b>Open SF2...</b> again.</p>

<h3>4. Remapping keys</h3>
<ul>
<li><b>File &rarr; Rebind Keys</b> — turn it on, click a piano key, then press the
keyboard key you want to assign to it. Changes are saved automatically.</li>
<li><b>File &rarr; Clear Key Binding</b> — turn it on, then press any mapped key to
remove its binding (so the key does nothing). Stays on so you can clear several;
turn it off when done.</li>
<li><b>File &rarr; Keymap Presets</b> — save the current layout under a name, or
switch between saved layouts.</li>
<li><b>File &rarr; Reset Keymap to Default</b> — restore the original layout.</li>
</ul>

<h3>5. Recording</h3>
<ul>
<li><b>Record &rarr; Start Recording</b> (Ctrl+R), play, then <b>Stop</b> (Ctrl+.).
You'll be asked to save a <code>.kps</code> file.</li>
<li><b>Record &rarr; Playback</b> (Ctrl+P) replays what you just recorded.</li>
</ul>

<h3>6. Settings &amp; language</h3>
<ul>
<li><b>File &rarr; Settings...</b> — pick the audio output device, sample rate and
buffer size (smaller buffer = lower latency).</li>
<li><b>Help &rarr; Language</b> — switch between English and 简体中文 at any time.</li>
</ul>
)");
}

QString usageGuideHtmlZh() {
    return QStringLiteral(R"(
<h2>keypiano —— 使用说明</h2>

<h3>1. 快速上手</h3>
<p>keypiano 把电脑键盘变成钢琴。打开程序后直接按键即可——它自带内置钢琴音色，开箱即响。</p>
<ul>
<li>两排字母键对应两个八度。默认 <b>Z</b> 这一排是左手（较低），<b>Q</b> 这一排是右手（较高）。</li>
<li>也可以用鼠标点击屏幕上的琴键发声。</li>
<li>踏板像真钢琴一样——<b>按住</b>对应键即生效：<b>Space</b> = 延音（制音踏板）、
<b>F3</b> = 持音（sostenuto）、<b>F4</b> = 弱音（柔音踏板）。<b>F1</b>/<b>F2</b> 整体降/升一个八度。</li>
</ul>

<h3>2. 更换音色</h3>
<p>有两种方式更换乐器音色：</p>
<ul>
<li><b>文件 &rarr; 打开 SF2...</b>——加载一个 SoundFont（<code>.sf2</code>）文件。
推荐免费的 GM 音色库，如 <i>GeneralUser GS</i>、<i>FluidR3</i>、<i>MuseScore_General</i>。</li>
<li><b>文件 &rarr; 打开 VST3 乐器插件...</b>——加载 VST3 插件（见下文）。</li>
</ul>

<h3>3. 使用 VST3 插件</h3>
<p><b>keypiano 本身不附带任何插件。</b>需要你自行下载安装，再让 keypiano 指向它。</p>
<ol>
<li>下载一个免费的 VST3 乐器，例如 <b>Surge XT</b>
（<a href="https://surge-synthesizer.github.io/">surge-synthesizer.github.io</a>）、
<b>Vital</b>、<b>Dexed</b> 等，运行其安装程序完成安装。</li>
<li>在 Windows 上，VST3 插件会以 <code>.vst3</code> 文件夹的形式安装到
<code>C:\Program Files\Common Files\VST3</code>。</li>
<li>在 keypiano 中选择 <b>文件 &rarr; 打开 VST3 乐器插件...</b>，选中那个
<code>.vst3</code> 文件夹（对话框会默认打开在标准 VST3 目录）。</li>
<li>用 <b>文件 &rarr; 显示插件编辑器...</b>（Ctrl+E）在插件内部挑选音色。该窗口是插件
自带的界面——用它的音色/预设浏览器选择，例如钢琴。（以 Surge 为例：点顶部的音色浏览器，
选一个 “Keys”/钢琴类预设。）</li>
</ol>
<p><b>注意：</b>踏板（延音/持音/弱音）等 MIDI CC 目前仅在内置 SF2 引擎下生效；
加载 VST3 插件时尚未下发这些控制——需要踏板请使用 SF2 引擎。</p>
<p>想切回内置音色，再次使用 <b>打开 SF2...</b> 即可。</p>

<h3>4. 重映射按键</h3>
<ul>
<li><b>文件 &rarr; 重绑按键</b>——开启后先点击一个琴键，再按下要绑定的键盘按键。改动会自动保存。</li>
<li><b>文件 &rarr; 清除按键绑定</b>——开启后按下任意已绑定的键即可移除它的绑定（让该键变成无效）。
该模式会保持开启以便连续清除多个，清完后再关闭。</li>
<li><b>文件 &rarr; 键位预设</b>——把当前布局以名字保存，或在多套已保存布局间切换。</li>
<li><b>文件 &rarr; 重置键位为默认</b>——恢复最初的布局。</li>
</ul>

<h3>5. 录制</h3>
<ul>
<li><b>录制 &rarr; 开始录制</b>（Ctrl+R），弹奏后按 <b>停止</b>（Ctrl+.），会提示保存
<code>.kps</code> 文件。</li>
<li><b>录制 &rarr; 回放</b>（Ctrl+P）重播刚才录下的内容。</li>
</ul>

<h3>6. 设置与语言</h3>
<ul>
<li><b>文件 &rarr; 设置...</b>——选择音频输出设备、采样率与缓冲大小（缓冲越小延迟越低）。</li>
<li><b>帮助 &rarr; 语言</b>——可随时在 English 与 简体中文 之间切换。</li>
</ul>
)");
}

}  // namespace keypiano::ui
