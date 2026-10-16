# snipaste

一个支持 Windows 与 macOS 的开源截屏、贴图和标注工具，操作方式和快捷键参考 Snipaste 2.x。

## 使用方法

启动后，程序常驻系统托盘，默认操作如下：

- **F1**：开始截图，框选区域后可标注、复制、保存或贴到屏幕。
- **F3**：将剪贴板中的图片或文字贴到屏幕。
- **Shift + F3**：隐藏或显示所有贴图。
- **Space**：选中贴图后显示或隐藏标注工具条；点击对号或按 **Enter** 完成编辑并保留贴图，**Esc** 优先收起工具条，再执行关闭操作。
- **Ctrl + 1…9**：选择形状、线条、画笔、荧光笔、马赛克、文字、序号、放大镜、橡皮擦；也可使用 **B / T / N / E** 选择画笔、文字、序号、橡皮擦。macOS 使用 Command 代替 Ctrl。
- **右键贴图**：切换标注显示、窗口阴影和置顶；菜单右侧显示快捷键，工具条悬浮提示显示对应按键。直接从截图贴到屏幕的标注可继续编辑，标注及显示设置会随贴图恢复。
- **右键托盘图标**：打开菜单，调整首选项、管理贴图或退出程序。
- **检查更新**：在托盘菜单或“首选项 → 更新”中检查，点击“立即更新”后自动下载、校验、安装并重启，无需手动解压。也可设置启动时和定期检查。

### Windows

普通用户推荐下载 `snipaste-版本号-windows-x64-setup.exe`，按向导安装。安装包自带 Qt，无需管理员权限，并提供开始菜单入口和卸载功能。

ZIP 便携版继续提供，解压后直接运行 `snipaste.exe`。两种版本都支持程序内更新。

从源码运行需要 Qt 6、CMake、Ninja、MSVC 和 Windows SDK。在项目根目录打开 PowerShell，执行：

```powershell
# 构建并启动
.\scripts\windows\run-dev.ps1

# 生成便携版 ZIP，输出到 dist/ 目录
.\scripts\windows\run-dev.ps1 -Configuration Release -Package

# 生成 EXE 安装包和 ZIP（需要安装 NSIS 3，或设置 NSIS_ROOT）
.\scripts\windows\package.ps1 -Installer
```

脚本默认使用 `D:/Qt/6.12.0/msvc2022_64`；Qt 安装在其他位置时，先设置 `QT_ROOT` 环境变量。

### macOS

安装 Xcode Command Line Tools 后，在项目根目录执行：

```bash
brew install qtbase cmake ninja
./scripts/macos/run-dev.sh
```

首次运行时，在“系统设置 → 隐私与安全性”中允许“屏幕录制”和“辅助功能”，然后重新启动程序。
