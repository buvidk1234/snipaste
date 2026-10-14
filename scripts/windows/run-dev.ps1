[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release')]
    [string]$Configuration = 'Debug',
    [switch]$Preferences,
    [switch]$Snip,
    [switch]$BuildOnly,
    [switch]$Test,
    [switch]$NoBuild,
    [switch]$Reconfigure,
    [switch]$Package
)

$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$buildDirectory = Join-Path $repoRoot ("build/windows-msvc-{0}" -f $Configuration.ToLowerInvariant())
$executable = Join-Path $buildDirectory 'snipaste.exe'

if ($Preferences -and $Snip) {
    throw 'Use either -Preferences or -Snip, not both.'
}
if ($Package -and ($Preferences -or $Snip -or $Test -or $BuildOnly)) {
    throw 'Do not combine -Package with -Preferences, -Snip, -Test, or -BuildOnly.'
}

# 自动关闭正在运行的旧版本，确保始终只运行最新的一个版本
$existing = Get-Process snipaste -ErrorAction SilentlyContinue
if ($existing) {
    Write-Host "Stopping existing snipaste process(es)..." -ForegroundColor Yellow
    $existing | Stop-Process -Force
    $existing | Wait-Process -Timeout 5 -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

$vsCandidates = @(
    'D:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat',
    'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\Tools\VsDevCmd.bat'
)
$vsDevCmd = $vsCandidates | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $vsDevCmd) {
    throw 'Visual Studio VsDevCmd.bat was not found in candidate paths.'
}

$qtRoot = $env:QT_ROOT
if ([string]::IsNullOrWhiteSpace($qtRoot)) {
    $qtRoot = 'D:\Qt\6.12.0\msvc2022_64'
}
if (-not (Test-Path -LiteralPath (Join-Path $qtRoot 'lib/cmake/Qt6/Qt6Config.cmake'))) {
    throw "Qt 6 was not found at $qtRoot. Set QT_ROOT to your Qt msvc2022_64 directory."
}

function Invoke-VsCommand([string]$command) {
    Write-Host "> $command" -ForegroundColor DarkGray
    $fullCommand = "call `"$vsDevCmd`" -arch=x64 >nul && $command"
    & cmd.exe /d /s /c $fullCommand
    if ($LASTEXITCODE -ne 0) {
        throw "Command failed with exit code ${LASTEXITCODE}: $command"
    }
}

$cacheFile = Join-Path $buildDirectory 'CMakeCache.txt'
if (-not $NoBuild -and ($Reconfigure -or -not (Test-Path -LiteralPath $cacheFile))) {
    New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null
    $source = $repoRoot.Replace('\', '/')
    $binary = $buildDirectory.Replace('\', '/')
    $qt = $qtRoot.Replace('\', '/')
    Invoke-VsCommand "cmake -S `"$source`" -B `"$binary`" -G Ninja -DCMAKE_BUILD_TYPE=$Configuration -DCMAKE_PREFIX_PATH=`"$qt`" -DSNIPASTE_BUILD_TESTS=ON"
}

if (-not $NoBuild) {
    if (-not (Test-Path -LiteralPath $cacheFile)) {
        throw "CMake build directory is not configured: $buildDirectory"
    }
    $binary = $buildDirectory.Replace('\', '/')
    $targetArg = if ($Test) { '' } else { '--target snipaste' }
    Invoke-VsCommand "cmake --build `"$binary`" $targetArg --parallel 4"
}

if (-not (Test-Path -LiteralPath $executable)) {
    throw "Executable not found: $executable"
}

if ($Package) {
    & (Join-Path $PSScriptRoot 'package.ps1') -BuildDirectory $buildDirectory -QtRoot $qtRoot
    exit 0
}

if ($Test) {
    $binary = $buildDirectory.Replace('\', '/')
    Invoke-VsCommand "ctest --test-dir `"$binary`" --output-on-failure"
    exit 0
}

if ($BuildOnly) {
    Write-Host "Build complete: $executable" -ForegroundColor Green
    exit 0
}

$qtBinDirectory = Join-Path $qtRoot 'bin'
$env:Path = "$qtBinDirectory;$env:Path"
$arguments = @()
if ($Preferences) { $arguments = @('open-preferences') }
if ($Snip) { $arguments = @('--snip') }

$psi = New-Object System.Diagnostics.ProcessStartInfo
$psi.FileName = $executable
$psi.WorkingDirectory = $buildDirectory
$psi.UseShellExecute = $false
if ($arguments.Count -gt 0) {
    $psi.Arguments = $arguments -join ' '
}
$process = [System.Diagnostics.Process]::Start($psi)
Write-Host "Started development build (PID $($process.Id)): $executable" -ForegroundColor Green
if ($Snip) {
    Write-Host "[Info] Snip mode activated - screenshot overlay is now open on your screen." -ForegroundColor Cyan
} elseif ($Preferences) {
    Write-Host "[Info] Preferences dialog opened." -ForegroundColor Cyan
} else {
    Write-Host "[Info] Snipaste is running in the background system tray (Windows taskbar notification area)." -ForegroundColor Cyan
    Write-Host "[Tip] Press 'F1' to snip, or run '.\scripts\windows\run-dev.ps1 -Snip' to launch screenshot overlay directly." -ForegroundColor Yellow
}
