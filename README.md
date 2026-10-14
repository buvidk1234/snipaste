# snipaste

一个支持 Windows 与 macOS 的开源截屏、贴图和标注工具，操作方式和快捷键参考 Snipaste 2.x。

## 使用方法

启动后，程序常驻系统托盘，默认操作如下：

- **F1**：开始截图，框选区域后可标注、复制、保存或贴到屏幕。
- **F3**：将剪贴板中的图片或文字贴到屏幕。
- **Shift + F3**：隐藏或显示所有贴图。
- **右键托盘图标**：打开菜单，调整首选项、管理贴图或退出程序。

### Windows

便携版解压后，直接运行 `snipaste.exe`。

从源码运行需要 Qt 6、CMake、Ninja、MSVC 和 Windows SDK。在项目根目录打开 PowerShell，执行：

```powershell
# 构建并启动
.\scripts\windows\run-dev.ps1

# 生成便携版 ZIP，输出到 dist/ 目录
.\scripts\windows\run-dev.ps1 -Configuration Release -Package
```

脚本默认使用 `D:/Qt/6.12.0/msvc2022_64`；Qt 安装在其他位置时，先设置 `QT_ROOT` 环境变量。

### macOS

安装 Xcode Command Line Tools 后，在项目根目录执行：

```bash
brew install qtbase cmake ninja
./scripts/macos/run-dev.sh
```

首次运行时，在“系统设置 → 隐私与安全性”中允许“屏幕录制”和“辅助功能”，然后重新启动程序。
