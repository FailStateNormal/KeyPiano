# keypiano 项目结构说明

> 这份文档解释仓库每个部分的作用：哪些是第三方库/插件、哪些用于编译构建、
> 哪些是我们自己写的核心代码、哪些是最终交付的产品。

---

## 一、我们自己写的核心代码（项目的本体）

全部在 `src/`，分两层：

### `src/core/` —— 核心引擎（不依赖 Qt，纯逻辑 + 音频 + 平台）
| 目录 | 作用 |
|------|------|
| `audio/` | 音频引擎：无锁队列 `LockFreeQueue.h`、`MidiEvent` 事件、`AudioEngine`（RtAudio WASAPI 回调，实时音频线程） |
| `synth/` | 合成器：`SynthesizerBase`（抽象接口）、`FluidSynthEngine`（SF2 音色）、`Vst3SynthEngine`（VST3 插件宿主）、`SynthFactory` |
| `keymap/` | 键位映射：`KeyMap`（键→音符/动作）、`KeyMapParser`（读 FreePiano .map）、`KeyMapSerializer`（写 .map） |
| `platform/` | 系统键盘钩子：`KeyboardHook` 接口 + `win32/Win32KeyboardHook`（全局低级钩子，过滤自动重复） |
| `recorder/` | 录制回放：`Recorder` + `KpsFormat`（.kps 文件读写） |
| `config/` | 配置文件：`AppConfig` + `CfgParser`（兼容 FreePiano .cfg） |

### `src/ui/` —— Qt 图形界面（依赖 Qt6 Widgets）
| 目录/文件 | 作用 |
|------|------|
| `MainWindow.*` | 主窗口、菜单、所有用户操作的入口（开 SF2/VST3、重绑、预设、录制） |
| `widgets/PianoWidget` | 自绘 88 键钢琴，鼠标点击发声、高亮 |
| `widgets/KeyboardOverlayWidget` | 键盘按键标签覆盖层 |
| `widgets/Vst3EditorWindow` | 嵌入 VST3 插件自带界面 |
| `bridge/AudioBridge` | 把音频线程的发声反馈搬到 UI 线程（驱动琴键高亮） |
| `dialogs/` | 各对话框：选 SF2、编辑标签、音频设置 |
| `main.cpp` | 程序入口 |

---

## 二、第三方库 / 插件（不是我们写的，外部依赖）

| 位置 | 是什么 | 怎么来的 |
|------|--------|----------|
| `vcpkg.json` 里声明的依赖 | **FluidSynth**（SF2 合成）、**RtAudio**（音频 IO）、**Qt6**（界面）、**vst3sdk**（VST3 宿主）、**GoogleTest**（测试） | 构建时由 vcpkg 自动下载编译，**不进仓库** |
| `vcpkg_installed/`、`build/*/vcpkg_installed/` | 上面这些库**编译好的产物**（.lib/.dll/头文件） | vcpkg 生成，已 .gitignore，**不进仓库** |
| `third_party/vst3_hosting/` | VST3 SDK 的 `module.cpp` / `module_win32.cpp`（加载 .vst3 用的 module loader） | 从 Steinberg VST3 SDK 复制（vendored），因为 vcpkg 端口没编译这部分。**进仓库**（GPLv3 合法） |
| 用户自己下载的 `.sf2` 音色、`.vst3` 插件（如 Surge XT） | **可选**的外部音色/插件 | 用户运行时通过 Open SF2 / Open VST3 加载，**不属于本项目**，仓库不依赖它们 |

> 关键点：仓库自带程序生成的 `default_piano.sf2`，所以**不下载任何插件也能出声**。
> Surge XT、mda-vst3 等只是「想用更好音色时的可选项」。

---

## 三、用于编译构建的部分（脚手架，不是产品本身）

| 位置 | 作用 |
|------|------|
| `CMakeLists.txt`（根 + 各 `src/*/CMakeLists.txt`、`tests/`） | CMake 构建脚本：定义 3 个目标（keypiano_core / keypiano_ui / keypiano），编译选项，资源打包，Qt 部署 |
| `CMakePresets.json` | 构建预设：windows-debug（headless 测试）、windows-gui-debug、windows-release 等 |
| `vcpkg.json` / `vcpkg-configuration.json` | 声明依赖库及版本、vcpkg baseline |
| `cmake/Vst3.cmake` | 手动配置 VST3 SDK 的 imported target（端口不导出 CMake config） |
| `.clang-format` | 代码风格 |
| `build/` | **编译输出目录**，已 .gitignore，**不进仓库** |
| `.verify/` | 临时验证用的一次性测试程序（如 SF2 衰减校验），已 .gitignore，用完即弃 |

构建工具链本身（**MSVC 编译器、vcpkg、Python**）装在系统里，不在仓库内。
本机具体路径见记忆 build-environment。

---

## 四、资源文件（随产品一起打包进 exe）

`resources/` 下的东西通过 Qt 资源系统（`keypiano.qrc` + AUTORCC）**编译进 exe**：

| 位置 | 作用 |
|------|------|
| `resources/keymaps/default.map` | 默认键位映射（左手 Z 行 C3、右手 Q 行 C4） |
| `resources/soundfonts/default_piano.sf2` | 程序生成的内置钢琴音色（开箱即响） |
| `resources/soundfonts/gen_default_piano.py` | 生成上面 SF2 的脚本（改音色后重跑它再构建） |
| `resources/icons/` | 工具栏/应用图标（PNG/ICO）+ 生成脚本 |

---

## 五、测试

| 位置 | 作用 |
|------|------|
| `tests/` | GoogleTest 单元测试（无锁队列、keymap 解析、cfg 解析、kps 格式、recorder）。`ctest --preset test-debug` 跑，目前 72/72 通过 |

---

## 六、最终的产品 / 交付结果

| 产物 | 位置 | 说明 |
|------|------|------|
| **GUI 调试版** | `build/windows-gui-debug/Debug/keypiano.exe` | 平时自测用，带调试信息 |
| **GUI 发行版** | `build/windows-release/Release/keypiano.exe` + 同目录全部 DLL + 插件子目录 | 自包含，可分发 |
| **免构建分发包** | `Desktop/keypiano-win64.zip`（按需打包） | 发行版 + MSVC 运行时（vcruntime140 等），别人解压直接双击运行，无需装 MSVC/Qt/vcpkg |

> 分发**必须用 Release**：Debug 版运行时（带 d 的 DLL）微软不允许再分发。

---

## 七、文档与其它

| 位置 | 作用 |
|------|------|
| `plan/TASKS.md` | 全项目任务清单 + 各阶段验收状态（开发主线） |
| `docs/build.md` | 构建步骤详解 |
| `docs/kps-format-spec.md` | .kps 录制格式规范 |
| `docs/project-structure.md` | 本文件 |
| `README.md` | 项目简介 |
| `LICENSE` | GPL v3 |
