# snipate

一个面向 Windows 与 macOS 的跨平台开源截屏、贴图和标注工具，目标是提供与 Snipaste 2.x 接近的操作模型、快捷键和界面行为。

项目已经完成 Windows 与 macOS 端首个可运行功能版本，并持续补齐 Snipaste 专业版能力。API、配置格式和文件格式暂不承诺稳定。

## 平台支持状态

- **Windows**: 已实现（Qt 6.12.0 + MSVC + Ninja，已支持完整截屏、贴图、标注、首选项及便携版发布）。
- **macOS**: 已适配（Carbon 全局热键、屏幕/窗口/元素捕获、Vision OCR、贴图跨 Space、登录启动）。

## 快速开始与构建 (Build & Run)

本项目构建核心采用 **CMake + `CMakePresets.json`**，日常开发工作流脚本归类在 `scripts/` 目录下按平台独立管理。

### 1. Windows 端 (PowerShell)

在 PowerShell 终端中执行（脚本自动关闭运行中的旧版进程，仅保留最新版本）：

```powershell
# 开发启动（默认 Debug 模式，构建并启动）
.\scripts\windows\run-dev.ps1

# 仅编译不启动
.\scripts\windows\run-dev.ps1 -BuildOnly

# 启动 Release 构建
.\scripts\windows\run-dev.ps1 -Configuration Release

# 运行自动化测试套件
.\scripts\windows\run-dev.ps1 -Test
```

### 2. macOS 苹果端 (Bash / Zsh)

在终端中执行（脚本自动关闭运行中的旧版进程，仅保留最新版本）：

```bash
# 开发启动（默认 Debug 模式，自动探测 Homebrew Qt6 或官方 SDK）
./scripts/macos/run-dev.sh

# 常用参数
./scripts/macos/run-dev.sh --release      # Release 模式
./scripts/macos/run-dev.sh --build-only  # 仅编译不启动
./scripts/macos/run-dev.sh --test        # 运行测试
```

### 3. IDE / 原生 CMake Presets

现代 IDE（VS Code、CLion、Qt Creator、Visual Studio）可直接选择预设进行一键构建与调试，或在终端执行：

- **Windows**: `cmake --preset windows-msvc-debug` / `cmake --build --preset windows-msvc-debug`
- **macOS**: `cmake --preset macos-debug` / `cmake --build --preset macos-debug`

## 推荐开发环境

- Visual Studio Code / CLion / Qt Creator
- Qt 6（桌面组件、Gui、Widgets、Concurrent、Network，按功能启用）
- CMake 3.28 或更高版本
- **Windows 平台**：Visual Studio 2026（MSVC v143 工具集）、Windows SDK、Ninja（Qt 路径示例：`D:/Qt/6.12.0/msvc2022_64`）
- **macOS 平台**：Xcode Command Line Tools、CMake、Ninja、Homebrew Qt 6（`brew install qtbase cmake ninja`）

当前已生成 Windows 便携版：`dist/snipate-0.1.0-windows-x64.zip`。解压后直接运行 `snipate.exe`。

首次在 macOS 上运行时，请在“系统设置 -> 隐私与安全性”中允许 snipate 使用“屏幕录制”和“辅助功能”。前者用于读取屏幕像素，后者用于全局快捷键、超级截屏和向前台应用发送 Cmd+V；修改权限后需重新启动 snipate。

## 文档

- [项目章程与范围](docs/00-project-charter.md)
- [功能需求规格](docs/01-requirements.md)
- [界面与交互规格](docs/02-ui-and-interaction.md)
- [快捷键规格](docs/03-keyboard-shortcuts.md)
- [系统架构与 CMake 组织](docs/04-architecture.md)
- [路线图与验收标准](docs/05-roadmap-and-acceptance.md)
- [开发进度记录](docs/06-development-log.md)
- [构建、打包与验证](docs/07-build-and-verify.md)
- [官方功能覆盖核对表](docs/reference/feature-coverage.md)
- [官方资料来源与快照说明](docs/reference/official-sources.md)
- [界面参考图说明](docs/reference/images/README.md)
- [功能实现状态](docs/reference/implementation-status.md)

## 设计目标

1. 截图、贴图、标注三条主流程可以在常驻托盘的单进程内快速切换。
2. 默认快捷键、键盘语义、鼠标手势、菜单名称和状态反馈与官方中文文档保持一致。
3. 所有功能通过可测试的命令、状态和数据模型表达，避免把行为散落在窗口事件处理器中。
4. Windows 与 macOS 实现优先使用 Qt 的跨平台抽象，只有屏幕捕获、全局热键、窗口检测、虚拟桌面和系统托盘等能力放入平台后端。

## 参考基线

功能基线来自 Snipaste 官方中文文档和更新日志。原始资料快照保存在 `docs/reference/snipaste-official/`，来源、版本边界和使用方式见 [官方资料来源与快照说明](docs/reference/official-sources.md)。
