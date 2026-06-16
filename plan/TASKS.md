# keypiano 任务清单

> 模型选择原则：实时音频路径 / 锁自由数据结构 / VST2 宿主 → Opus 4.8；其余 → Sonnet 4.6

---

## Phase 1 — 核心引擎（能响）

### P1-A  工程骨架  【Sonnet 4.6】
- [x] `CMakeLists.txt`（根）：三个 target（keypiano_core / keypiano_ui / keypiano）
- [x] `src/core/CMakeLists.txt`
- [x] `src/ui/CMakeLists.txt`
- [x] `tests/CMakeLists.txt`（KEYPIANO_ENABLE_TESTS 开关）
- [x] `CMakePresets.json`（windows-debug / windows-release / windows-relwithdebinfo）
- [x] `vcpkg.json` + `vcpkg-configuration.json`
- [x] `cmake/FindVST2.cmake`（Phase 4 用，先占坑）
- [x] `.gitignore`（CMake build 目录、vcpkg、MSVC 产物）
- [x] `.clang-format`（Google 风格基准 + 微调）
- [x] `LICENSE`（GPL v3 全文）
- [x] `README.md`（最小版：项目简介、构建步骤）

### P1-B  无锁队列  【Opus 4.8】
- [x] `src/core/audio/LockFreeQueue.h`
  - MPSC 队列（容量 1024，用于 event_queue：Hook+UI → Audio）
  - SPSC 队列（容量 256，用于 feedback_queue：Audio → UI）
  - header-only 模板，指定 acquire/release 内存序
  - 音频线程侧：零 malloc / 零等待
- [x] `tests/test_lockfree_queue.cpp`（多线程压测 + 边界测试）

### P1-C  MidiEvent POD  【Sonnet 4.6】
- [x] `src/core/audio/MidiEvent.h`
  - 字段：type / chan / note / vel / ts_us
  - 枚举：EventType（NoteOn / NoteOff / ControlChange / AllNotesOff）

### P1-D  合成器接口 + FluidSynth 实现  【Sonnet 4.6】
- [x] `src/core/synth/SynthesizerBase.h`（纯虚接口，见 PLAN_DETAIL.txt 第二节）
- [x] `src/core/synth/SynthFactory.h`（工厂函数 createFluidSynth / createVstSynth）
- [x] `src/core/synth/FluidSynthEngine.h/.cpp`
  - init / shutdown / loadInstrument（.sf2 路径）
  - noteOn / noteOff / controlChange / allNotesOff / render
  - 不在音频线程做任何阻塞调用

### P1-E  音频引擎  【Opus 4.8】
- [x] `src/core/audio/AudioEngine.h/.cpp`
  - 持有 event_queue（MPSC）和 feedback_queue（SPSC）
  - open / close / postNoteOn / postNoteOff / postControlChange / postAllNotesOff
  - Stats：buffer_underruns / cpu_load / latency_us
    > 实现说明：Stats 提为 `keypiano::audio` 命名空间内的独立 struct（非嵌套
    > `AudioEngine::Stats`），因为 CallbackContext 需在 AudioEngine 定义前引用它。
  - synth 非拥有：调用方创建并保证存活；open() 内按协商后的 buffer_frames 调 synth->init()
- [x] `src/core/audio/AudioCallback.h/.cpp`
  - RtAudio WASAPI 回调：drain event_queue → synth → render
  - 写 feedback_queue 通知 UI
  - 全程零 malloc / 零锁等待 / 零 Qt 调用
  - 已用 `/W4 /WX /permissive-` 编译通过 + 真实 WASAPI 流冒烟测试通过

### P1-F  Win32 键盘钩子  【Sonnet 4.6】
- [x] `src/core/platform/KeyboardHook.h`（纯虚接口 + 工厂 `create()`）
- [x] `src/core/platform/win32/Win32KeyboardHook.h/.cpp`
  - SetWindowsHookExW(WH_KEYBOARD_LL) + 独立消息循环线程
  - 回调只做：查表 + 写 event_queue，不做任何 I/O
  - 必须在 300ms 内返回
  - 验收：MSVC /W4 /WX 构建通过；keypiano.exe 集成测试中钩子安装/卸载正常

### P1-G  键位映射  【Sonnet 4.6】
- [x] `src/core/keymap/KeyMap.h`
  - KeyBinding 结构体（vk_code / action / channel / midi_note / step / label）
  - ChannelState（octave_offset / velocity / sustain / **key_signature**）
    > 注意：key_signature 移到 ChannelState 而非 KeyMap，支持运行时动态移调
  - `resolve(vk_code, is_keydown, ch0, ch1) → optional<MidiEvent>`
- [x] `src/core/keymap/KeyMap.cpp`
- [x] `src/core/keymap/KeyMapParser.h/.cpp`
  - 版本检测（第一行）→ 分流 2.0 / 2.1 解析器
  - 2.0：小写键名、数字通道、小写音符、兼容拼写错误 "KeyboardVeolcity"
  - 2.1：PascalCase 键名、In_0/In_1 通道、大写音符、功能键完整支持
- [x] `src/core/keymap/KeyMapSerializer.h/.cpp`（序列化回 2.1 格式）
  - 验收时发现 src/core/CMakeLists.txt 漏接此文件，已修复
- [x] `tests/test_keymap_parser.cpp`
  - 覆盖：2.0/2.1 语法、拼写错误兼容、音符解析、resolve 移调、序列化往返
  - 验收时发现 tests/CMakeLists.txt 漏接此测试，已修复；并修正 1 处测试 bug
    （ResolveOutOfRangeClamped 误用 C0 应为 C-1）
  - 验收：20/20 通过（g++ + GoogleTest）

### P1-H  配置文件解析  【Sonnet 4.6】
- [x] `src/core/config/AppConfig.h`
  - 结构体：sf2_path / keymap_path / audio_device / sample_rate / buffer_frames
- [x] `src/core/config/AppConfig.cpp`
- [x] `src/core/config/CfgParser.h/.cpp`（兼容 FreePiano .cfg 格式）
- [x] `tests/test_cfg_parser.cpp`
  - 验收：11/11 通过（g++ + GoogleTest）

### P1-I  Phase 1 集成入口  【Sonnet 4.6】
- [x] `src/main.cpp`（Phase 1 版，无 GUI）
  - 加载 .cfg → 初始化 AudioEngine + FluidSynth → 安装 KeyboardHook
  - 标准输入等待 'q' 退出
  - 验收：实跑通过——加载 cfg→载入 SF2→开 WASAPI 引擎→装钩子→'q' 干净退出（见下方验收状态）

---

## Phase 1 验收状态（2026-06-14）—— ✅ 全部通过

验收环境：MSVC 2022（14.44.35207 / cl 19.44）+ vcpkg（C:\vcpkg）+ 本机 WASAPI 音频设备。
走完了 plan §8 的官方 P1 验收命令，并补充了运行期 render 实测。

### 官方构建链（plan §8）

| 步骤 | 命令 | 结果 |
|------|------|------|
| 配置 | `cmake --preset windows-debug` | ✅ vcpkg 装 fluidsynth+rtaudio+gtest（无 Qt），MSVC 识别正常 |
| 构建 | `cmake --build --preset windows-debug` | ✅ keypiano.exe + 3 个测试，`/W4 /WX` 干净 |
| 测试 | `ctest --preset test-debug` | ✅ **41/41 通过** |

### 运行期实测（render 冒烟 + 集成入口）

| 项 | 结果 |
|----|------|
| FluidSynth 加载真实 SF2 + noteOn + render | ✅ 输出峰值幅度 0.1219（非零，**确实出声**） |
| AudioEngine 打开真实 WASAPI 设备 | ✅ 协商缓冲 256 帧 |
| 音频回调实跑统计 | ✅ cpu_load=0.2%、**latency≈5.8ms（< 20ms 目标）**、underruns=0 |
| keypiano.exe 集成入口 | ✅ 加载 cfg→载入 SF2→开引擎→装钩子→'q' 干净退出（exit 0） |

> 唯一未由机器执行的是"人耳听声 + LatencyMon 系统级 DPC 延迟工具"——属主观/硬件环节；
> 引擎侧缓冲延迟 5.8ms 已远低于 20ms 目标，render 峰值非零已证明音频确实产生。

### 验收中发现并已修复的缺陷（5 个）

1. `CMakeLists.txt`（根）：`add_compile_options($<$<CONFIG:Release>:/O2;/GL>)` 中 genex
   内的 `;` 被当成列表分隔符，拆出畸形 flag，**直接导致 MSVC 构建失败**
   （C1083: Cannot open source file '$<0:/O2'）。已拆成各自独立的 genex。
2. `src/core/CMakeLists.txt` 漏接 `keymap/KeyMapSerializer.cpp`（已写完却被注释为"未实现"）
3. `tests/CMakeLists.txt` 漏接 `test_keymap_parser`（已写完却被注释为"未编写"）
4. `tests/test_lockfree_queue.cpp` include 写成 `core/audio/...`，但 keypiano_core 暴露的
   include 根是 `src/core`，真实构建编不过；改为 `audio/LockFreeQueue.h`。
5. `tests/test_keymap_parser.cpp` 的 `ResolveOutOfRangeClamped` 用例误用 C0（应为 C-1）。

### 顺带的工程改进

- `vcpkg.json`：把 `qtbase` 移入 `gui` feature、`gtest` 移入 `tests` feature，核心只留
  fluidsynth+rtaudio。这样 headless 的 P1/P2 构建不再被迫从源码编译整个 Qt6。
- `CMakePresets.json`：各预设通过 `VCPKG_MANIFEST_FEATURES` 按阶段选择 feature
  （debug→tests，release/relwithdebinfo→gui）。

---

## Phase 2 — 录制回放

> 前置：Phase 1 全部通过验收

### P2-A  KPS 格式  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/core/recorder/KpsFormat.h/.cpp`
  - 读写 [meta] + [events] 段
  - UTF-8 文本，微秒精度时间戳
  - 时间戳相对于录制开始时刻（不是绝对 epoch）
- [x] `docs/kps-format-spec.md`（正式格式规范）
- [x] `tests/test_kps_format.cpp`（往返读写一致性测试）
  - 11 个测试用例：往返、四种事件类型、空事件、大时间戳、含=号标题、CRLF、错误路径、格式错误
  - 验收：ctest 52/52（全套，含 P1 原有 41 + P2-A 新增 11）

### P2-B  Recorder 模块  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/core/recorder/Recorder.h/.cpp`
  - 状态机：Idle → Recording → Idle / Idle → Playing → Idle
  - `onMidiEvent`：仅在 Recording 状态追加到内部 vector（零 malloc，预分配 65535 容量，index 计数）
  - 回放线程：独立线程 sleep_until + DispatchFn（生产用 makeAudioDispatch 包 AudioEngine，测试用 lambda）
  - saveToFile / loadFromFile（委托 KpsFormat）
  - 修复了析构时 joinable std::thread 触发 std::terminate 的问题（stopPlayback/dtor 始终 join）
- [x] `tests/test_recorder.cpp`（录制"小星星" → 保存 → 加载 → 回放时序验证）
  - 20 个测试：状态机、录制边界、容量限制、文件存取、回放派发顺序、时序（±60ms）、中止、全场景
  - 验收：ctest 72/72（全套，含 P1 41 + P2-A 11 + P2-B 20）

---

## Phase 3 — Qt GUI

> 前置：Phase 1 + Phase 2 全部通过验收

### P3-A  主窗口骨架  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/ui/MainWindow.h/.cpp/.ui`
  - 菜单：File（Open SF2、Open Keymap、Edit Keymap、Settings、Exit）/ Record（Start/Stop/Playback）
  - 工具栏：录制/停止/回放按钮（带 SVG 图标）
  - 状态栏：延迟 / CPU 占用 / 当前音色名

### P3-B  AudioBridge  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/ui/bridge/AudioBridge.h/.cpp`
  - QTimer 16ms 轮询 feedback_queue
  - emit `noteActivated(int midi_note)` / `noteReleased(int midi_note)`
  - 连接到 PianoWidget 槽函数

### P3-C  PianoWidget  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/ui/widgets/PianoWidget.h/.cpp`
  - QPainter 自绘 88 键（MIDI 21-108）
  - 白键 52 个，黑键覆盖相邻白键间隙，高度 60%，宽度 55%
  - 按下：白键 #6BB5FF / 黑键 #2255AA
  - 松开：淡出（QTimer 16ms tick，每帧 alpha -= 17）
  - 鼠标点击发声（mouseNoteOn/Off 信号 → MainWindow → event_queue）
  - 每键可叠加显示 label；新增 keyRect() 供 P3-D 查询

### P3-D  KeyboardOverlayWidget  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/ui/widgets/KeyboardOverlayWidget.h/.cpp`
  - 显示键盘按键映射到音符的 label 覆盖层
  - 叠加在 PianoWidget 上方（透明背景 + WA_TransparentForMouseEvents）
  - eventFilter 跟随 PianoWidget resize；updateFromKeyMap() 从 Note 绑定取 label

### P3-E  设置对话框  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `src/ui/dialogs/SoundFontDialog.h/.cpp`（浏览 .sf2 + QSettings 最近文件列表）
- [x] `src/ui/dialogs/KeyMapEditorDialog.h/.cpp`（表格视图，Label 列可编辑）
- [x] `src/ui/dialogs/SettingsDialog.h/.cpp`（RtAudio 枚举设备、采样率、缓冲帧；OK→restartEngine 立即生效）

### P3-F  资源文件  【Sonnet 4.6】  ✅ 2026-06-14
- [x] `resources/keypiano.qrc`
- [x] `resources/keymaps/default.map`（QWERTY 两个八度 C4–B5 + 八度/延音控制；验证零解析错误）
- [x] `resources/icons/`（app.ico/app.png 钢琴图案、record/play/stop.svg；ICO 内嵌进 exe）
- [x] MainWindow 启动自动加载 :/keymaps/default.map（loadDefaultKeymap）

---

## Phase 3 验收状态（2026-06-14）—— ✅ 全部通过

- headless 核心测试 **72/72 通过**（GUI 改动不影响 windows-debug）
- `default.map` 经临时单元测试确定性验证：**0 解析错误 + 24 音符 + 3 控制 + 27 label**（测试已撤）
- GUI 构建 `/W4 /WX` 干净（修复 1 处 C4458：局部变量 `data` 遮蔽 QWidget::data）
- keypiano.exe（GUI）启动冒烟测试通过：窗口起来、默认 keymap 无阻塞加载、干净退出
- 验收中补的缺口：default.map 资源原本无人加载，已加 loadDefaultKeymap() 启动自动加载

---

## Phase 4 — VST3 + 打包

> 前置：Phase 3 全部通过验收
>
> **方向变更（2026-06-14）：原计划用 VST2，已改为 VST3。**
> 原因：Steinberg 2018-10 终止 VST2 许可且禁止再分发，新项目无法合法获取；
> VST3 SDK 是 GPLv3/专有双许可，本项目是 GPL v3，走 GPLv3 一支完全合法，
> 且 SDK 公开可经 vcpkg 拉取，**无需手动提供 SDK 路径**。
> 核心 `SynthesizerBase` 抽象不变，只重写 P4-A/P4-B + CMake；
> 旧占位 `cmake/FindVST2.cmake` 废弃（P4-A 落地时删除）。

### P4-A  VST3 宿主  【Opus 4.8】  ✅ 2026-06-14
- [x] vcpkg 集成 `vst3sdk`（vcpkg.json 新增 `vst3` feature；preset features 加 vst3；无需手动路径）
- [x] `cmake/Vst3.cmake`：端口不导出 CMake config，手动建 `keypiano::vst3` imported target
      （include + debug/release 分目录链接 sdk_hosting/sdk/sdk_common/base/pluginterfaces）
- [x] `src/core/synth/Vst3SynthEngine.h/.cpp`（pimpl 隐藏所有 SDK 类型）
  - `VST3::Hosting::Module` 加载 `.vst3` bundle；手动 `createInstance<IComponent>` + initialize
    （未用 PlugProvider，省去 connectionproxy 依赖；controller 留 P4-B）
  - 取 `IAudioProcessor`，setBusArrangements(stereo) / setupProcessing / activateBus / setActive / setProcessing
  - SynthesizerBase 映射：noteOn/noteOff → `Vst::Event`(NoteOn/NoteOff) 入预分配 EventList(512)；
    render → `IAudioProcessor::process()` 后 additive mix 输出总线；active[16][128] 跟踪供 allNotesOff
  - controlChange 暂 no-op（VST3 CC 需 IMidiMapping，留后续）
  - 音频线程安全：EventList/HostProcessData 预分配，零 malloc / 零锁
- [x] `createVst3Synth()` 工厂函数（`#ifdef KEYPIANO_ENABLE_VST3`）
- [x] 删除废弃 `cmake/FindVST2.cmake` + root CMakeLists 的 `KEYPIANO_VST2_SDK_PATH`

> **关键坑**：vcpkg `vst3sdk` 端口装了 hosting 头文件 + 库，但**没编译 module loader**
> （`Module::create` / module_win32 缺失）。解决：从 SDK 复制 `module.cpp` + `module_win32.cpp`
> 到 `third_party/vst3_hosting/`（GPLv3 分支，自包含），单独用 `/std:c++17` 编译
> （C++20 下 `filesystem::u8string()` 返回 char8_t 与 SDK 的 char 路径不兼容）。
>
> **验收**：临时单测加载真实 `mda-vst3.vst3` → noteOn → render 200 块，**峰值 0.17560（确实发声）**；
> headless 72/72 不受影响（windows-debug 无 vst3 feature 时 VST3_FOUND=FALSE，core 不含 VST3 代码）。

### P4-B  VST3 编辑器窗口  【Opus 4.8】  ✅ 2026-06-14
- [x] `Vst3SynthEngine::load()` 创建并接线 `IEditController`（getControllerClassId →
      factory 创建独立 controller，或单组件回退用 component；initialize + setComponentHandler +
      IConnectionPoint 双向 connect）。最小 `IComponentHandler` + `IPlugFrame`（U::ImplementsNonDestroyable）
- [x] `IEditController::createView("editor")` 取 `IPlugView`，`attached(HWND, kPlatformTypeHWND)`
- [x] 嵌入 Qt：`Vst3EditorWindow`（QWidget，子容器设 WA_NativeWindow，winId() 作 HWND 父窗口）
- [x] 处理 plug-view resize 回调（IPlugFrame::resizeView → 经 queued 信号 marshall 到 GUI 线程）
- [x] 接口边界：核心新增 `synth/IPluginEditor.h`（无 SDK/Qt 类型），UI 用 dynamic_cast 探测；
      MainWindow 加 "Show Plugin Editor"（Ctrl+E，仅 VST3 后端启用），切后端前先 closePluginEditor()
> 验收：`.verify/vst3_editor_smoke.cpp`（临时目标，已撤）用真实 mda-vst3 + 真实 Win32 HWND 验证：
> loadInstrument+setupController 不崩、hasEditor=true、openEditor 优雅返回 false（mda 无自定义界面）、
> closeEditor/shutdown 不崩、**controller 接线后 render peak=0.16096 仍发声**。带 GUI 的插件嵌入因本机
> 无此类插件未做可视验证（代码按 VST3 hosting 规范实现）。

### P4-C  打包配置  ✅ 2026-06-14
- [x] CMakeLists.txt post-build 自动部署 Qt：windeployqt 存在则用，否则（vcpkg Qt 不带
      windeployqt）按 `$<CONFIG:Debug>` genex 从 vcpkg_installed 拷 platforms/styles/imageformats。
      修了直接跑 exe 崩 "no Qt platform plugin" 的问题（见 [[build-environment]]）
- [x] `docs/build.md` 更新：feature 表加 vst3、preset 表加 windows-gui-debug、第 5 节改为
      "自包含部署"说明、删 VST2 SDK 章节换成 VST3（无需手动 SDK）+ vendored 源说明。
      README.md 同步（VST2→VST3、阶段状态、自包含 Release）
- [x] 验收：Release（windows-release，gui;vst3）构建产物用**剥离 PATH（仅 System32）**启动正常 →
      自包含通过；release 插件 qwindows.dll（无 d）+ 全部运行时 DLL 齐全；VST3 发声路径见 P4-A/P4-B
      （render peak 0.16~0.17）。带 GUI 的开源插件（Surge XT 等）本机未装，未做可视验收

---

## Phase 5 — 易用性打磨（2026-06-15 实测中发现，用户体验向）

> 背景：用户亲自跑 GUI 时暴露若干问题。下面【已修】的是本次会话已完成并重新构建验证，
> 【待办】是下次开新对话继续做的。

### P5-fix  本次已修的真 bug（2026-06-15）✅
- [x] **AUTORCC 没开 → 资源全失效**（最严重）：`qt_standard_project_setup()` 不开 AUTORCC，
      `keypiano.qrc` 从未编译 → `:/keymaps/default.map` 加载失败 → keymap_ 空 → **键盘完全没反应**；
      工具栏图标也空白。修：root CMakeLists `set(CMAKE_AUTORCC ON)` + qrc 移到 keypiano 可执行目标
      （避免静态库资源初始化被链接器丢弃）。验证：resource_check 5/5 资源 open=YES，工具栏图标恢复。
- [x] **切后端 use-after-free**：onOpenSf2/onOpenVst3 先析构旧 synth_ 再 restartEngine，音频线程
      仍持旧指针 render + close() 对已析构对象调 shutdown → 崩溃（从 Surge 切 SF2 必崩）。
      修：restartEngine 拆成 stopEngine()+startEngine()，顺序改 closePluginEditor→stopEngine→换 synth_→startEngine。
- [x] **黑键映射偏左一格**：默认 map 黑键原为 A S F G H / 1 2 4 5 6，物理上 Z 右上是 S 不是 A。
      修正为 S D G H J（C#4 D#4 F#4 G#4 A#4）/ 2 3 5 6 7（八度5）。
- [x] **启动无默认音色**（FluidSynth 必须有 SF2 才发声）：用 `resources/soundfonts/gen_default_piano.py`
      程序生成一个 16KB 钢琴 SF2（bank0/preset0），打包进 qrc；启动时 `loadDefaultSoundFont()` 释放到
      `%APPDATA%\keypiano\default_piano.sf2` 并加载（render peak 0.077 够响）。**坑：SF2 音量包络
      generator 别硬调，最简 igen(只 loop+sampleID)交给 FluidSynth 默认包络最稳**。
- [x] VST3 Open 对话框默认开在 `C:\Program Files\Common Files\VST3` + 记住上次（QSettings vst3_dir）。
- [x] 装了 Surge XT（winget SurgeSynth.SurgeXT），实证 VST3 编辑器嵌入成功（1141x711）+ 发声 0.347。

### P5-A  可手动编辑按键映射（2026-06-15 完成）✅
用户定方案：**做方案 2（琴键点击重绑）+ 方案 3（多套预设，仅保留当前这套）**，重绑后**自动持久化**。
- [x] **默认映射整体左移一个八度**：Z 行 C4→**C3**（左手）、Q 行 C5→**C4**（右手），黑键同步下移。
      `resources/keymaps/default.map` 已改 + 注释更新（左手/右手标注）。
- [x] **方案 2 琴键点击重绑**：菜单 File → "Rebind Keys"（可勾选）开启重绑模式。
      `PianoWidget` 加 `setRebindMode/setSelectedKey` + `keyClickedForRebind` 信号 + 橙色选中高亮；
      重绑模式下点琴键不发声、只选中。`MainWindow` 状态机：点琴键 `onRebindKeyClicked` 置
      `rebind_armed_`(atomic) → 键盘钩子线程抓下一个 keydown → `QMetaObject::invokeMethod` 队列
      marshall 回 UI 线程 `applyRebind(vk)` → 改 `keymap_`（移动语义：先删该音符旧 channel-0 绑定，
      避免重复）→ overlay 刷新 → 存盘。**只能重绑音符键，控制键由预设覆盖（方案 2 固有边界）**。
- [x] **自动持久化**：`KeyMap::removeBinding` 新增；`KeyMapSerializer::keyName` 暴露为 public
      静态（vk→键名，复用进 serialize）。重绑后 `saveUserKeymap()` 写
      `%APPDATA%\keypiano\user.map`；启动 `loadStartupKeymap()` 优先加载 user.map（解析失败回退默认）。
- [x] **Reset Keymap to Default** 菜单项：删 user.map + 重载内置 default.map（防止自定义弹崩了出不来）。
- [x] **（2026-06-15 追加）Clear Key Binding 清除单个绑定**：用户反馈"只能绑不能清"。File→"Clear Key
      Binding（按一个键即清除）"可勾选模式，与 Rebind 互斥；开启后按任意键 → `KeymapController::tryCaptureClear`
      （hook 线程 atomic）→ marshal `applyClear` 删该 vk 绑定+发布快照+存 user.map；模式保持开启可连清多个。
- [x] **（2026-06-15 追加）移除"Edit Keymap Labels"**：用户判断——初衷是改映射，结果做成只改琴键显示标签、
      不动映射的半成品，自我误导。Rebind（改映射）+Clear（删映射）已覆盖真实需求，故彻底删除
      `KeyMapEditorDialog.h/.cpp` + 菜单项/槽/I18n 条目。标签本身仍随 keymap 自动显示（重绑时 label=键名），
      只是去掉"手动编辑标签"这个入口。
- [x] 验收：windows-gui-debug 构建通过、headless **72/72** 仍过；KeyMapEditorDialog 改名 "Edit Keymap **Labels**" 以区分。
> 注：方案 3 用户只要当前「双手各一八度」这套，未另造预设——现有 Open Keymap（Ctrl+K）即可切换其它 .map。

### P5-fix2  默认音色「长按晃」+ 分发包（2026-06-15）✅
- [x] **长按声音一直晃**：根因不是混响（reverb/chorus 已关），是默认 SF2 为「稳态谐波音 + 循环」，
      循环点周期非整数样本 → 每次回绕相位跳变 → 长按约 5Hz 抖动。改 `gen_default_piano.py`：
      生成 **2.5s 带衰减的一次性采样（不循环，sampleModes 缺省=0）**，衰减烘焙进 PCM、谐波各自衰减、
      4ms 起音淡入 + 20ms 收尾淡出防爆音。`MainWindow::loadDefaultSoundFont` 改为**每次启动覆盖**
      `%APPDATA%\keypiano\default_piano.sf2`（旧逻辑只在不存在时释放，会被旧缓存遮蔽）。
      验收：`.verify/sf2_decay_check.cpp` 实测 加载 OK、peak 0.063 有声、rms@2.0s/rms@0s=0.36（确实衰减不再稳态）、无 NaN。
- [x] **自包含分发包（给没有 MSVC 的人）**：`cmake --build --preset windows-release` 产物
      （build/windows-release/Release：exe + 19 运行库 DLL + platforms/styles/imageformats 插件）
      自包含；额外打包 MSVC CRT 运行时（vcruntime140/_1、msvcp140）→ `Desktop\keypiano-win64.zip`（40.5MB）。
      对方解压直接双击 keypiano.exe，无需 MSVC/vcpkg/Qt。Debug 运行时不可分发——必须用 Release。

### P5-fix3  SF2 对话框 + 默认音色没声音（2026-06-15）✅
- [x] **最近文件列表删不掉**：SoundFontDialog 原本只有 Browse/OK/Cancel。加 **Remove（删选中）+ Clear All
      （清空）** 按钮；选中项才启用 Remove。loadRecents 时**自动剔除已不存在的文件路径**并回写 QSettings。
- [x] **选 SF2 报错**：根因就是最近列表残留了不存在的旧测试 SF2 路径，点它 loadInstrument 失败。
      上面的自动剔除 + 手动删除解决；真实存在的 SF2 加载本就正常。
- [x] **默认打开没声音**：根因有二——(1) FluidSynth 默认 `synth.gain` 仅 0.2 太小；(2) 上版衰减太快。
      修：`FluidSynthEngine::init` 设 `synth.gain=0.5`（对所有音色生效）；`gen_default_piano.py` 衰减
      时间常数拉长（基频 tau 1.3→2.8s）、时长 2.5→3.5s。验收：`.verify/sf2_decay_check.cpp`（gain=0.5）
      实测 **peak 0.063→0.158**、rms@2s=0.051 清晰可闻。重建 gui-debug+release，重打 `Desktop\keypiano-win64.zip`(40.6MB)。

### P5-B  使用说明 + 中英文切换（2026-06-15）✅
- [x] **Help 菜单**：新增 `setupHelpMenu()`。"Usage Guide"(F1) 弹 QDialog+QTextBrowser，
      内嵌中英双语 HTML 使用说明，**含"如何自行下载并使用 VST3 插件"**（Surge XT 等，本程序不附带插件，
      引导到标准 VST3 目录 + Show Plugin Editor 选音色）+ 基础弹奏/换音色/重绑/录制/设置。
      "About keypiano" 关于框（双语）。
- [x] **中英文切换**：Help → Language（English / 简体中文，radio）。因 vcpkg Qt **不带 lrelease/lupdate**
      （无法生成 .qm），改用自包含方案：`src/ui/I18n.h/.cpp` 子类化 `QTranslator`，内嵌英→中映射表
      （override `translate()` 按源文本查表、`isEmpty()` 返回 false 强制生效）。`installTranslator`/
      `removeTranslator` 切换 → 触发 `QEvent::LanguageChange` → `MainWindow::changeEvent` →
      `retranslateUi()` 重设所有代码内菜单/动作/工具栏/状态标签文本。按需创建的对话框
      （SoundFont/Settings/KeyMap/录制提示）因构造时 tr() 即被翻译，自动跟随当前语言。
      语言选择存 QSettings(`keypiano/keypiano/language`=en|zh)，下次启动 `loadLanguageSetting()` 恢复。
- [x] 配套：状态栏乐器名抽成 `refreshSf2Label()`（统一翻译"(built-in)"后缀）；菜单/工具栏指针提升为成员。
- [x] 验收：windows-gui-debug 构建 `/W4 /WX` 干净；中、英两种语言启动均不崩；headless **72/72** 仍过。
      **未做人工 GUI 实测切换观感**（待用户跑）。

### P5-C  键位方案（map 预设）保存/删除/切换（2026-06-15）✅
- [x] 菜单 File → "Keymap Presets" 子菜单：动态列出 `%APPDATA%\keypiano\presets\*.map`（点击即加载并设为当前），
      + "Save Current As..."（QInputDialog 取名，非法字符替换为 _，存在则确认覆盖）
      + "Delete Preset..."（QInputDialog 列表选择 + 确认）。复用 KeyMapSerializer/KeyMapParser。
      加载预设后 saveUserKeymap() 写 user.map，重启后保持。验收：gui-debug 构建通过。

### P6  三踏板模拟（2026-06-15，Opus 4.8）✅
> 关键设计：踏板**不在 keypiano 层模拟**，而是解析成标准 MIDI CC 事件，交由 FluidSynth 原生处理
> （CC64 sustain / CC66 sostenuto / CC67 soft 全部原生支持）。踩下=CC127、松开=CC0，**momentary**
> （按住即踩、松开即抬，像真踏板）。这是最稳路径：复用现有 `controlChange→fluid_synth_cc`。
- [x] **三个新 KeyAction**：`SustainPedal`(CC64) / `SostenutoPedal`(CC66) / `SoftPedal`(CC67)（KeyMap.h）。
      保留旧 `SustainSet`（latch，仅记状态）兼容老 map。
- [x] **KeyMap::resolve 处理踏板**：两个边沿都产 `ControlChange`——keydown→{note=cc, vel=127}、
      keyup→{vel=0}。八度/调号不作用于踏板（通道级控制）。**MainWindow 无需改**：installHook 已有
      `case ControlChange → postControlChange`，录制回放 `makeAudioDispatch` 也已转发 CC。
- [x] **关键前提已具备**：Win32 钩子已过滤 OS 自动重复（前沿 keydown+配对 keyup），所以踏板按一次=一个
      CC127、松一次=一个 CC0；CC66 sostenuto 只在按下瞬间快照一次（不会因重复 keydown 反复抓取）。
- [x] **Parser/Serializer**：新增 `SustainPedal`/`Sostenuto`/`Soft`(`SoftPedal`) 关键字（2.0+2.1 同分支），
      语法 `Keydown <key> SustainPedal <In_0|In_1>`；序列化补三个 case（/WX 下 switch 必须全覆盖）。
- [x] **默认 map**：`Space`→SustainPedal、`F3`→Sostenuto、`F4`→Soft（momentary，按住生效），更新头注释。
- [x] **单元测试**：解析三踏板动作、缺通道报错、resolve down/up 产正确 CC(64/127→0)、CC66/67 编号、
      序列化往返。**77/77 全过**（原 72 + 5 新）。
- [x] **离线发声验证**：`.verify/pedal_check.cpp`（cl+vcpkg libfluidsynth-3）对比松键后 0.6-0.7s 余音
      RMS——**不踩 0.00000 / 踩下 0.07336**，证明 CC64 确实延迟 noteOff 让音延续。VST3 后端 CC 仍为
      no-op（与既有限制一致，延后）。
- [x] 文档：使用说明（中英）补踏板用法；windows-gui-debug 构建干净、冒烟启动加载新 default.map 不崩。
- [x] **（2026-06-15 追加）踏板指示灯 + 踏板可改键**（用户要求）：
      - 新增 `src/ui/widgets/PedalIndicatorWidget`（QWidget 自绘）：钢琴下方一排三格（延音/持音/弱音），
        踩下亮绿、显示各踏板当前绑的键（未绑显示"(无)"）。central 改为 QVBoxLayout(piano 拉伸 + pedal 固定 58)。
        踏板灯亮灭由 hook 路径检测 CC64/66/67 后 marshal 到 UI 线程点亮。
      - **踏板改键**：rebind 流程扩展——重绑模式下点踏板格（`onRebindPedalClicked`）→ 按一个键 → 绑成该踏板
        （`applyRebind` 按 `rebind_pedal_cc_` 分支，move 语义删旧键）。原来"只能绑音符"的限制解除。
        `updatePedalLabels()` 在 keymap 变更时刷新灯下的键名。I18n 补延音/持音/弱音等。
      - 验收：DPI 感知截图实证三踏板格渲染正确（用户当前 user.map 缺 Space → "延音(无)"，正好暴露问题）。
        **踩踏板手感人工实测仍待用户跑。**
      > 验证踩坑记：本机 150% 缩放，PowerShell 默认 DPI 不感知 → CopyFromScreen/GetWindowRect 坐标错位，
      > 整窗截图会把底部状态栏+踏板条截掉。**截图脚本必须先 `SetProcessDPIAware()`**，否则误判控件没渲染。
- [x] **（2026-06-16 追加）弱音真正生效 + 踏板模式设置**（用户反馈持音/弱音没影响）：
      - **离线实测**（`.verify/pedal_check2.cpp`）：**持音 CC66 FluidSynth 原生正常**（按住音→踩→松键，rms 0.071
        继续响；踩了再弹的音 0.0 不保持），用户没注意是因为持音本身隐蔽有条件。**弱音 CC67 FluidSynth 根本没实现**
        （踩不踩 rms 完全一样 ratio 1.0）→ 改在**我们层**实现：踩下弱音时把后续 NoteOn 力度压到 60%
        （handleKeyboardEvent，hook 线程读 `pedal_engaged_[soft]` atomic）。
      - **踏板模式** File→"Pedal Mode"：Hold（按住生效）/ Toggle（按一下踩、再按一下抬，键盘更顺手）。
        `pedal_mode_`(atomic) + `pedal_engaged_[3]`(atomic)；handle() 仍出 momentary CC，handleKeyboardEvent
        按模式转换（toggle 时丢 keyup、翻转 engaged）。切模式/引擎重启时释放已锁踏板防止卡住。QSettings 持久化。
      - 灯亮灭、录制力度都跟着走；使用说明（中英）补踏板模式+持音用法。gui-debug /W4 /WX 干净 + 冒烟不崩。
- [x] **（2026-06-16 追加）默认右手键位改为 Y U I O P [ ] / 7 8 0 - =**（用户定，更顺手）：
      default.map 右手八度4 白键 `Y U I O P LBracket RBracket`=C4–B4、黑键 `7 8 0 Minus Equals`=C#4–A#4
      （左手 Z 行不变）。`.verify/map_check.cpp` 用核心库实测：**0 错误、29 绑定、右手全部解析正确**。
      使用说明（中英）右手描述改为 "Y row (Y U I O P [ ])"。**弱音系数 60%→35%**（用户反馈 60 不明显；
      默认 SF2 力度曲线陡，35% 接近真实 una corda 且清晰）。
      ⚠️ 注意：旧 `%APPDATA%\keypiano\user.map` 会优先于 default.map 加载 → 用户需 **Reset Keymap to Default**
      才能用上新默认（或继续用自己的 user.map）。

---

## Phase 7 — 健壮性与功能闭环（2026-06-15，Opus 4.8，按用户架构审查）✅
> 用户给了一份分层审查。只做"真风险 + 功能闭环 + 最安全的结构整理"，压住过度抽象，每步可编译可测可回退。

### P7-1  线程安全：keymap 快照（消除 data race）✅
- [x] **根因**：hook 线程在 `resolve()` 里读 `keymap_`，UI 线程在 load/edit/rebind/preset 时 `std::move`
      重写它 → 并发读+重写 vector/unordered_map 是 UB。
- [x] **修法**：`MainWindow` 加 `std::atomic<std::shared_ptr<const KeyMap>> keymap_ptr_`。UI 线程保留可变
      `keymap_` 工作副本，每次改动后 `publishKeymap()` 存一份不可变快照；hook 线程 `keymap_ptr_.load()`
      读快照调 `handle()`。所有 keymap 变更收口到 `setActiveKeymap()`（load/edit/preset/open）+ applyRebind
      后 publishKeymap（顺带去重 overlay/enable 样板）。
- [x] **附带修一个 UAF**：`stopEngine()` 原先先 reset `recorder_` 再 uninstall hook → hook 回调正用着
      recorder_ 就被释放。改为**先 uninstall hook（join 线程）再拆 bridge/recorder/engine**。

### P7-2  KeyAction 功能闭环（八度/力度/调号/Record 真正生效）✅
- [x] **根因**：parser/serializer 早支持这些动作，但执行层 `resolve()` 对非 Note 返回 nullopt、hook 直接丢
      → 默认 map 的 F1/F2 八度、velocity **从来没生效过**（界面承诺但实际没有）。
- [x] `KeyMap::handle(vk, down, ch0&, ch1&)` 新入口返回 `KeyResult{optional<MidiEvent> midi; bool toggle_record}`：
      Note/踏板→委托 resolve；Octave/Velocity/KeySignature→就地改 ChannelState；Record→toggle_record；
      旧 `SustainSet`→keydown 发 CC64=step（兼容老 map）。`*this` 不改，可在快照上安全调。
- [x] **clamp 集中**：ChannelState 加 `shiftOctave[-4,4]/shiftVelocity[1,127]/setVelocityValue/
      shiftKeySignature[-6,6]`，唯一限幅处，调用方不再散落 clamp。
- [x] hook 改用 `handle()`；Record 命令经 `QMetaObject::invokeMethod` queued 回 UI 线程
      `toggleRecordFromHotkey()`（复用 onStartRecording/onStop）。原大 lambda 抽成 `handleKeyboardEvent()`。
- [x] 测试 +5：八度位移改音高、力度位移改 NoteOn vel、Record 仅 keydown 触发、旧 SustainSet 发 CC64、
      ChannelState 三项 clamp。**headless 82/82**（72→77→82）。

### P7-3  结构整理（只做最安全一块）✅
- [x] **HelpContent 拆出**：使用说明 HTML 从 MainWindow.cpp 移到 `src/ui/HelpContent.h/.cpp`
      （`keypiano::ui::usageGuideHtmlEn/Zh`），MainWindow 瘦约 90 行。
- [x] **VST3 诚实**：使用说明（中英）补一句"踏板/CC 目前仅 SF2 引擎生效，VST3 暂不下发"。
- [x] **KeymapController 拆出**（用户定：只拆这一个，放 src/ui 做 QObject 子类）：
      `src/ui/KeymapController.h/.cpp` 接管 keymap 工作副本 + 原子快照 + user.map/preset 持久化 + 点击重绑。
      对 hook 线程暴露线程安全 `snapshot()`/`tryCaptureRebind()`；UI 槽用 `status(QString,int)` 信号发回状态栏。
      MainWindow 仅保留 hook 通道状态 ch0_/ch1_ + `handleKeyboardEvent`（碰 engine/recorder）。
      action/preset_menu 仍由 MainWindow 建（菜单序+retranslate），triggered 连到控制器槽。
      **MainWindow.cpp 1044→715 行**（−327）。gui-debug /WX 干净 + 冒烟启动不崩（未做人工键入/重绑实测）。
- [未做/有意压住] SynthController/RecordingController、SynthSession、SynthCapabilities 结构体——
      牵涉音频/VST 生命周期或属过早抽象，留后续增量。`record_start_` 已有 seq_cst happens-before，非 bug，未动。
- [x] 验收：windows-gui-debug `/W4 /WX` 干净 + 冒烟启动不崩；headless 82/82。

### P7-3 增量修复（2026-06-16，用户分层 review）✅
> 背景：P6 的踏板系列功能（指示灯/重绑/模式/弱音）在 P7-3 抽出 KeymapController 之后才陆续加入，
> 直接堆回 MainWindow（715→813 行）。用户做了一次分层 review，列出 5 个真问题 + 结构走向。
> 本轮只修这 5 个（低风险、可独立验证），结构整理另作决策（见末条）。
- [x] **#1 鼠标信号重复连接（真 bug）**：`mouseNoteOn/Off` 在启动 `setupPianoWidget` 连一次，
      之后**每次 `startEngine()` 又连一次** → 切 SF2/VST3/改设置后，鼠标点屏幕琴键发多次 NoteOn（叠加放大）。
      修：鼠标 connect 收口到 `setupPianoWidget` **只连一次**（去掉 `if(engine_)` 守卫、改 lambda 内 `if(!engine_)return;`，
      兼顾启动时引擎打开失败、后续重启成功的情况）；`startEngine` 删除鼠标 connect，只保留 bridge→piano 重连
      （bridge 每次新建，连的是新对象，无重复）。
- [x] **#2 失败提示与行为不符**：`startEngine` open 失败提示 "Reverting to defaults"，实际只 `engine_.reset()`+空
      recorder，并未用默认配置重开。**不做真 fallback**（默认配置可能同样失败→递归弹窗），改文案为
      "Audio is stopped — reopen it from Settings."（中文映射同步 I18n.cpp）。
- [x] **#5 VST3 踏板文案不精确**：`softVelocity` 在 `postNoteOn` 前套用、**不分后端**，所以**弱音在 VST3 下仍生效**；
      只有 sustain/sostenuto 走 CC 而 VST3 后端 `controlChange` no-op。Help（中英）改为：VST3 下延音/持音无效、
      弱音（力度衰减实现）仍有效。
- [x] **#3 clear 模式吞所有 keydown（UX）**：`tryCaptureClear` 原本 clear 开着就吞任意键再提示 "not bound"。
      改：hook 线程先读不可变快照 `snapshot()->find(vk)`，**未绑定键静默放行**（本就不发声），不再刷状态栏。
- [x] **#4 踏板重绑无 label（这轮只设 label）**：重绑 pedal binding 设 `Sus/Sost/Soft`（ASCII，可序列化进 user.map）。
      ⚠️ **用户原诊断有偏差**：`KeyboardOverlayWidget` 只渲染 `KeyAction::Note` 绑定，**任何踏板（默认/重绑）本就不进 overlay**，
      设 label 不会让它在 overlay 显示；踏板键名实际在踏板灯条显示，默认与重绑一致。"让 overlay 渲染踏板" 是独立后续项，本轮未做。
- [结构决策] **PedalController 抽取——评估后搁置**：踏板那 ~90 行深度耦合实时 hook 事件流（`process()` 要在 NoteOn 管线中途改
      `ev`），抽出会在热路径加间接、风险不抵收益；813 行未到警戒线。**下一个结构整理目标定为 `RecordingController`**
      （recorder 生命周期 + record/stop/playback + `toggleRecordFromHotkey`，边界清晰、不碰实时 atomic）；`SynthController`
      牵 engine 生命周期 + VST editor，最后动。
- [x] 验收：windows-gui-debug `/W4 /WX` 干净 + headless **82/82** + 冒烟启动 4s 存活正常关闭。**人工 GUI 实测待用户跑。**

### P7-3 增量：默认音色换真钢琴 GeneralUser GS（2026-06-16）✅
> 用户反馈合成的 `default_piano.sf2` 太假；Surge 等合成器也合不出真实钢琴。结论：钢琴音色走
> SF2 + FluidSynth 最合适（采样音色、无 VST3 的 CC 限制）。用户实测真钢琴音满意，权衡后同意分发
> 包从 ~40MB 涨到 ~70MB（30MB SF2 的运行时 RAM 占比相对 Qt+VST3 宿主很小）。
- [x] **音色**：GeneralUser GS（S. Christian Collins，自定义宽松授权，允许免费使用+再分发），
      完整 GM 库 259 preset，preset 0 = 原声大钢琴（正好是引擎默认音色）。下到 `resources/soundfonts/GeneralUser-GS.sf2`（30.8MB）。
- [x] **不进 qrc**（30MB 会撑大 exe）：CMakeLists POST_BUILD `copy_if_different` 把 SF2 拷到 exe 旁 `soundfonts/`，
      `EXISTS` 守卫——缺文件也能构建（回退合成钢琴）。
- [x] **`loadDefaultSoundFont()` 两级**：① 优先加载 `applicationDirPath()/soundfonts/GeneralUser-GS.sf2`；
      ② 缺失则回退内置合成钢琴（qrc 释放到 %APPDATA%）。状态栏显示 `SF2: GeneralUser-GS.sf2 (built-in)`。
- [x] **离线验证**（复用 `.verify/sf2check.exe`）：加载 OK、peak 0.0745 出声、rms 比 0.231 自然衰减（真采样钢琴特征）。
      gui-debug 构建通过 + SF2 部署 30.8MB + 冒烟启动正常。
- [备注] SF2 已提交进 git（仓库 +30MB，历史永久）；规范做法是 Git LFS，对个人项目过度工程，未用。

### P7-3 增量：抽出 RecordingController（2026-06-16）✅
> 兑现上面「下一个结构整理目标定为 RecordingController」的结构决策。照 KeymapController 已验证的
> 套路抽成 `src/ui/RecordingController.{h,cpp}`（QObject 子类，MainWindow 的子对象）。
- [x] **职责**：接管 Recorder 所有权 + 录制/回放流程（含保存对话框）+ Record/Stop/Playback 三个 action 的
      启用态 + 热键 toggle + hook 线程喂事件。UI 槽 `startRecording/stop/startPlayback/toggleFromHotkey`，
      状态用 `status(QString,int)` 信号发回状态栏（复用 MainWindow 现成的转发 lambda）。
- [x] **生命周期解耦**：recorder 由 MainWindow 构建（它持 engine 供 dispatch），经 `setRecorder()` 交给控制器；
      `clearRecorder()` 停播放/录制并释放。MainWindow 保证**先卸 hook（join 线程）再 clearRecorder**，
      所以 hook 线程的 `feed()` 永不与指针 swap/reset 竞态（与抽取前同一不变式）。
- [x] **closeEvent 重排**：原来「停 recorder → 卸 hook」改为「**卸 hook → 停 bridge → clearRecorder → 关 engine**」，
      保证 recorder（其回放 dispatch 指向 engine_）在 engine_ 之前销毁，消除潜在 UAF。
- [x] **happens-before 保留**：`record_start_` 仍在 `startRecording()` 里于 `recorder_->startRecording()`（seq_cst
      store state_）之前写；`feed()` 读 state_(acquire)==Recording 后再读 record_start_。语义逐字不变。
- [x] **I18n 不受影响**：自定义 Translator 忽略 tr() context、只按源文本匹配（I18n.h 注释），录制相关字符串
      移到新类后中文仍生效。
- [x] **MainWindow.cpp 813→758 行**（−55；录制方法整体搬走，扣除新接线净减）；MainWindow.h 同步瘦身。
- [x] 验收：windows-gui-debug `/W4 /WX` 干净 + headless **82/82** + 冒烟启动正常。**录制/回放人工实测待用户跑。**
- [结构决策更新] 剩 `SynthController`（engine 生命周期 + VST3 editor + 后端切换 + SF2/VST3 加载），耦合最深，
      留最后。本轮未动。

### P7-3 增量：质量加固待办（2026-06-16，按收益/成本排序）
> 用户 review 给出一组加固项，让我按判断排优先级并先动手。结论顺序如下（① 本轮立即做）：
- [x] **① 中文路径 / Unicode 安全（2026-06-16，已完成）**——**真 bug，非理论风险**。根因：MSVC 的 `std::fstream`
      用 `const char*` 构造时按当前 ANSI 代码页（中文系统 = GBK）解释路径，而 Qt `toStdString()` 给的是
      UTF-8，两者不匹配 → 中文路径打不开。受影响：
        - 录音 `.kps` 存/读 `KpsFormat.cpp:52/88`（`std::ofstream/ifstream(path)`）——存到中文目录或中文文件名失败/乱码。
        - 键位 `user.map`/预设 读 `KeymapController.cpp:135/180/450`（`std::ifstream`）——**写用 QFile（安全）读用 ifstream（不安全）不对称**；
          中文 Windows 用户名 → AppData 路径含中文 → 键位静默退回默认。
      安全的：VST3（Steinberg `StringConvert` 当 UTF-8 转宽字符）、SF2（FluidSynth 2.x 内部转宽字符走 `_wfopen`）。
      改法：KeymapController 三处读取改用已在用的 `QFile`（与写入侧统一）；KpsFormat 是 core 不依赖 Qt，
      改成接受宽路径（`MultiByteToWideChar` + MSVC `wchar_t*` fstream 重载）。局部改动，不碰大架构。
      **实现**：KeymapController 三处（loadStartup/openKeymap/loadPreset）改用匿名命名空间的 `readTextFile()`（QFile），
      删掉不再用的 `<fstream>/<sstream>`；KpsFormat 加 `_WIN32` 守卫的 `widenUtf8()`，write/read 在 Windows 用宽路径。
      新增测试 `KpsFormatUnicode.ChinesePathRoundTrip`（文件名用显式 UTF-8 字节，不依赖源码编码）。
      验收：gui-debug `/W4 /WX` 干净 + headless **83/83**（+1 新测试）+ 冒烟启动正常。
      [遗留] `src/main.cpp:34` 的 `std::ifstream` 是 headless 测试入口、非用户路径，未改。
- [x] **② VST3/SF2 加载失败回滚（2026-06-16，已完成）**——VST3 加载失败会把原本能用的 SF2 后端丢掉。
      **设计约束**：原计划「候选 synth 成功后再替换」需先离线加载候选再让 engine open，但 `AudioEngine::open()`
      必调 `synth->init()`，而 `FluidSynthEngine::init()` 会新建 synth（assert !synth_）冲掉已加载的 font →
      纯预加载需改 core 接口（让 open 跳过已就绪 synth），不算「局部」。故改用**等价的用户级保证**：尝试切换，
      失败即回滚重建原后端，用户永不失去能用的后端，且代价只落在罕见的失败路径。
      **实现**（全在 MainWindow，零 core 接口改动）：
        - 新增 `rebuildBackend(target)`（stop→换 synth→start）、`currentInstrument()`（快照 backend/path/name/builtin）、
          `restoreInstrument(snap)`（重建+重载+刷 label，重载失败兜底回内置默认钢琴）；新成员 `current_vst3_path_`。
        - `onOpenVst3`：先快照，`rebuildBackend(Vst3)+loadInstrument` 成功才更新；失败 `restoreInstrument(prev)` 回滚。
        - `onOpenSf2`：VST3→SF2 走 rebuild+回滚；同后端换 SF2 仍走 live load（见下，已无缝且安全）。
        - **core 局部改 1 处**：`FluidSynthEngine::loadInstrument` 改为**先 sfload 新 font 成功再 sfunload 旧 font**
          （原来先卸后载，失败即静默）——同后端换音色失败时旧音色保留，无音频间断。
      验收：gui-debug `/W4 /WX` 干净 + headless **83/83** + 冒烟启动正常。**切后端失败回滚人工实测待用户跑。**
- [x] **③ 队列丢事件统计（2026-06-16，已完成）**——`event_queue_.push()` / `feedback_queue->push()` 失败原本静默丢。
      **实现**：`Stats` 加 `event_drops`（输入事件丢弃 = 漏音/迟音，producer 线程写）+ `feedback_drops`（UI 高亮丢弃 = 纯视觉，
      audio 线程写），均 relaxed atomic。`AudioEngine::post` push 失败 `fetch_add`；AudioCallback 用本地 lambda
      `pushFeedback`（无分配、wait-free）统一三处 feedback push 的计数。MainWindow 状态栏加 `lbl_drops_`（红字，
      **仅 drop>0 时显示**："Drops: N in / M ui"），`updateStatus()` 每 500ms 刷新；I18n 加中文「丢弃：输入 %1 / 界面 %2」。
      计数随 engine 重开归零（新 Stats）。验收：gui-debug `/W4 /WX` 干净 + headless **83/83** + 冒烟启动正常。
- [x] **④ VST3 踏板限制提示（2026-06-16，已完成）**——真正实现 `IMidiMapping` 成本高，暂不做。
      VST3 后端 `controlChange` 是 no-op，延音/持音踏板灯会亮但无声 → 用户以为坏了。**实现**：`handleKeyboardEvent`
      踏板块里，当 `engage && current_backend_==Vst3 && (idx==0||1)`（延音/持音）时，marshal 到 UI 线程在状态栏提示
      4 秒「VST3 乐器不支持延音/持音踏板（弱音踏板仍然有效）」；I18n 加中文。软踏板(idx 2)走 velocity 缩减、不提示。
      `current_backend_` 在 hook 线程读安全（切后端先卸 hook）。验收：gui-debug `/W4 /WX` 干净 + 冒烟启动正常。
- [x] **⑤ 抽 SynthController（2026-06-16，已完成）**——把 synth/engine/bridge 生命周期 + 后端切换回滚 +
      SF2/VST3 加载 + VST3 编辑器整体搬进 `src/ui/SynthController.{h,cpp}`（QObject 子类，MainWindow 子对象）。
      **保留时序铁律**：engine 拆/装是唯一线程敏感处（audio 回调 + hook 线程都摸 engine_/synth_）。控制器通过宿主
      提供的两个**同步回调**在与旧 `stopEngine/startEngine` 完全相同的时点调用，顺序不变：
        - `Host::beforeEngineStop`（每次拆卸最先调）：卸 hook（join 线程）+ 释放 recorder，再由控制器停 bridge、关 engine。
        - `Host::afterEngineStart`（重启后调，engine 为 null 表示打开失败）：重建 recorder → 装 hook → 复位踏板 →
          重连 bridge→piano。recorder 先于 hook，杜绝 hook 线程喂到旧 recorder。
      **MainWindow 仅留** hook_（其回调 handleKeyboardEvent 在 MainWindow）+ 踏板/钢琴控件 + 状态栏 label。
      构造顺序调整：先 `setupPianoWidget()` 再 `initialize()`，使首次启动时 afterEngineStart 能连上已存在的钢琴控件
      （删掉 setupPianoWidget 里旧的初始 bridge→piano 连接，统一由 afterEngineStart 负责）。
      `onOpenSf2/onOpenVst3` 仅留对话框，加载+回滚委托给 `loadSf2/loadVst3`（错误文案经 *err 返回给 MainWindow 弹窗）。
      **MainWindow.cpp 879→约 685 行**；新增 SynthController ~330 行。验收：gui-debug `/W4 /WX` 干净 +
      冒烟启动 5s 存活、CloseMainWindow 有序拆卸退出码 0 + headless **90/90**。**切后端/回滚/插件编辑器人工实测待用户跑。**
      [遗留] engine 仍直建真 `AudioEngine`（WASAPI），故后端切换/UAF/hook 生命周期的 headless 测试仍需引入
      `IAudioEngine` 注入缝才能做，本轮未引入（属更大重构）。
- [x] **⑥ 生命周期/失败路径测试（2026-06-16，完成本阶段可做部分）**：
        - **已补**：`tests/test_audio_engine.cpp`（3 个）测 ③ `event_drops`；`tests/test_audio_callback.cpp`（4 个，本轮）
          测 ⑥ 的 `feedback_drops`——`audioCallback` 是自由函数，用桩 synth + 自建 CallbackContext/队列直接驱动，
          **无需音频设备**：fq 填满后 drain 事件即丢弃并精确计数、有空位不丢且转发、ControlChange 不产生反馈、初值 0 且每次 render 一次。
        - **已覆盖（之前）**：KpsFormat 读缺失/写坏路径/中文路径往返；Recorder 缺文件/超容量。
      **仍缺（需 ⑤ 之上再引入 IAudioEngine 注入缝）**：后端切换失败回滚（②）、切后端 UAF、hook 生命周期——
      SynthController 已隔离这些逻辑，但 startEngine 仍开真 AudioEngine，headless 测需注入假引擎。
      验收：headless **90/90**（+4）。
- [暂缓] Qt `.ts/.qm` 国际化迁移（手写 I18n 仍能撑）、完整 VST3 CC/踏板实现（涉及参数映射+插件兼容，先提示限制）。
- [x] **bugfix：鼠标点击的音符现在能被录制（2026-06-16）**——根因：键盘走 `handleKeyboardEvent→feed()`，
      而 `setupPianoWidget` 的鼠标 lambda 只 `engine_->postNoteOn/Off`、漏喂 recorder。修：鼠标 lambda 也
      `rec_ctl_->feed(MidiEvent{...})`。**线程安全**：`Recorder::onMidiEvent` 是单生产者，而键盘 feed 在 hook 线程、
      鼠标 feed 在 UI 线程 → 给 `RecordingController::feed` 加 `feed_mutex_` 串行化两个生产者（均非实时线程，
      不违反音频铁律）。验收：gui-debug `/W4 /WX` 干净 + headless **83/83** + 冒烟启动正常。

---

## 跨阶段注意事项

- **每个 Phase 开始前**先用 Opus 4.8 做一次架构审查（特别是线程边界处的数据流）
- **音频线程铁律**贯穿所有阶段，每次提交前 grep 检查 audio callback 里有无 `new`/`malloc`/锁等待
- **key_signature 运行时可变**：已从 KeyMap 移到 ChannelState，实现 KeyMapParser 时注意不要重新放回 KeyMap 字段
- **Recorder::onMidiEvent 的 malloc 问题**：预分配 vector（如 65535 个事件容量），用 index 计数，不用 push_back
- **回放线程时序**：Recorder 回放用独立线程 + `std::this_thread::sleep_until`，通过 AudioEngine::post*() 写 event_queue，不直接调用 synth
