# snipaste

一个截屏、贴图和标注工具。

## 使用

从 [GitHub Releases](https://github.com/buvidk1234/snipaste/releases) 下载对应平台的安装包，包内已包含 Qt 运行库。

- Windows：运行 `windows-x64-setup.exe`，以管理员权限安装到 `C:\Program Files\Snipaste`（可自选目录），完成后从开始菜单启动；也可解压 `windows-x64.zip`，直接运行 `snipaste.exe`。
- macOS：解压 `macos-universal.zip`，将 `snipaste.app` 拖入“应用程序”。当前安装包使用 ad-hoc 签名，尚未公证。首次运行需在系统设置中授予“屏幕录制”和“辅助功能”权限。

- **F1**：截图。
- **F3**：将剪贴板内容贴到屏幕。

macOS 的功能键默认控制亮度等系统功能时，使用 `Fn+F1` / `Fn+F3`，或在系统设置中启用“将 F1、F2 等键用作标准功能键”。截图需要“隐私与安全性 → 屏幕与系统音频录制”权限；模拟粘贴、窗口元素识别和滚动截图还需要“辅助功能”权限。更改权限后重新启动应用。

升级或重新构建后，如果权限已开启但仍被拒绝，请在系统设置中移除旧的 snipaste 授权记录，再添加当前运行的 `snipaste.app`，然后完全退出并重新打开应用。存在同名应用时，请核对路径，确保授权的是当前运行的这一份。

更多操作可自行探索。

## 构建

构建配置只有两个文件：[CMakeLists.txt](CMakeLists.txt) 定义构建、测试和打包规则，[CMakePresets.json](CMakePresets.json) 保存平台和 Debug / Release 参数。本地和 CI 使用相同命令，产物位于 `build/<preset>/`。

依赖：CMake 3.28+、Ninja、Qt 6.12+、C++20 编译器。3.28 是最低要求，CMake 4.x 可以直接使用。

### Windows

安装 Visual Studio 2022 或更新版本的“使用 C++ 的桌面开发”工作负载（含 Windows SDK），以及 Qt 的 MSVC 2022 x64 组件。在 **Developer PowerShell for VS** 中执行：

```powershell
$env:QT_ROOT = 'C:/Qt/6.12.0/msvc2022_64' # 改为实际路径
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
```

### macOS

安装 Xcode Command Line Tools 和上述依赖后执行：

```bash
export QT_ROOT="$HOME/Qt/6.12.0/macos" # 改为实际路径
cmake --preset macos-debug
cmake --build --preset macos-debug
```

将 Preset 名称中的 `debug` 改为 `release` 即构建 Release。Debug 默认只编译主程序，Release 默认同时编译测试。macOS Debug 使用本机架构，Release 使用 `x86_64;arm64` 通用架构，最低支持 macOS 14.4。

### 运行、测试、打包

以下以 Windows 为例；macOS 使用对应的 `macos-debug` / `macos-release`：

```powershell
cmake --build --preset windows-msvc-debug --target run
cmake --build --preset windows-msvc-debug --target all
ctest --preset windows-msvc-debug
cmake --build --preset windows-msvc-release --target package
```

`run` 会编译并启动程序，等待从托盘菜单退出程序后才结束。运行测试前用 `--target all` 编译测试程序。同一构建目录中的构建命令应依次执行。

打包前先配置并构建 Release，发布包输出到 `dist/`，包含 Qt 运行库和 SHA-256 校验文件。Windows 还需要 NSIS 3（`makensis.exe` 在 `PATH` 中，或设置 `NSIS_ROOT`）。macOS 当前使用 ad-hoc 签名，尚未公证。

只构建应用可在配置时加 `-DBUILD_TESTING=OFF`。个人配置放入不纳入版本控制的 `CMakeUserPresets.json`。`scripts/` 中仅保留安装和自动更新代码。

## 自动检查与发布

[CI](.github/workflows/ci.yml) 在提交和 PR 时构建、测试并检查安装；OCR 测试依赖系统环境，单独运行并允许失败。

macOS 测试还会用临时签名应用验证自动更新的校验、替换、失败回滚、配置保留和重启；安装检查会验证通用架构、签名和部署后的独立启动。

更新 `CMakeLists.txt` 中的版本后，推送匹配的 `vX.Y.Z` 或 `vX.Y.Z-rc.1` 标签，由 [Release](.github/workflows/release.yml) 构建、测试、打包并上传 GitHub Releases。发布到其他仓库时设置 `SNIPASTE_RELEASE_REPOSITORY` 变量和 `RELEASE_TOKEN` Secret。
