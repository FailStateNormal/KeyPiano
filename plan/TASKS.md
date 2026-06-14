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

### P5-A  可手动编辑按键映射（待办，下次开新对话做）⬜
> 目的：用户想按左右手习惯自定义键位（左手左下、右手右上）。现有 KeyMapEditorDialog **只能改
> 标签文字，不能重绑键→音符**。三个候选方案（下次选一个做）：
> 1. **完整可编辑**：现有 Editor 每行可"按下新物理键重绑（抓键）"+ 下拉选音符，能增删行，
>    Apply 即时生效。最灵活，工作量最大。需扩展 KeyMapEditorDialog + 可能加抓键控件 +
>    KeyMapSerializer 存自定义 .map（持久化到 QSettings 或用户目录）。
> 2. **琴键上点击重绑**：点钢琴键选中 → 按物理键 → 绑定。直观，专为"调手感"，但不能改控制键。
> 3. **多套预设 map**：准备几套 .map（右手为主 / 左右手各一八度），用现有 Open Keymap 切换。
>    不做 UI，最快。
> 注：KeyMapSerializer 已存在（写回 .map）；KeyMapParser 支持完整 FreePiano 格式。

### P5-B  Surge/VST3 音色选择引导（可选）⬜
- [ ] 用户反馈 Surge 界面看不懂（那是插件自己的 GUI）。可在 docs 或 README 加"如何在 Surge
      Patch Browser 选钢琴音色"的简短说明；或长远做插件内 program 下拉。

---

## 跨阶段注意事项

- **每个 Phase 开始前**先用 Opus 4.8 做一次架构审查（特别是线程边界处的数据流）
- **音频线程铁律**贯穿所有阶段，每次提交前 grep 检查 audio callback 里有无 `new`/`malloc`/锁等待
- **key_signature 运行时可变**：已从 KeyMap 移到 ChannelState，实现 KeyMapParser 时注意不要重新放回 KeyMap 字段
- **Recorder::onMidiEvent 的 malloc 问题**：预分配 vector（如 65535 个事件容量），用 index 计数，不用 push_back
- **回放线程时序**：Recorder 回放用独立线程 + `std::this_thread::sleep_until`，通过 AudioEngine::post*() 写 event_queue，不直接调用 synth
