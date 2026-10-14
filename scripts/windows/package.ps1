[CmdletBinding()]
param(
    [string]$BuildDirectory,
    [string]$QtRoot = $env:QT_ROOT,
    [string]$Version
)
$ErrorActionPreference = 'Stop'
$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if (-not $BuildDirectory) { $BuildDirectory = Join-Path $repoRoot 'build/windows-msvc-release' }
if (-not $QtRoot) { $QtRoot = 'D:/Qt/6.12.0/msvc2022_64' }
if (-not $Version) {
    $cache = Get-Content -LiteralPath (Join-Path $BuildDirectory 'CMakeCache.txt') -Raw -Encoding UTF8
    $match = [regex]::Match($cache, '(?m)^SNIPASTE_RELEASE_VERSION:[^=]+=(.+)$')
    if ($match.Success) { $Version = $match.Groups[1].Value.Trim() }
    else {
        $cmake = Get-Content -LiteralPath (Join-Path $repoRoot 'CMakeLists.txt') -Raw -Encoding UTF8
        $Version = [regex]::Match($cmake, 'project\(snipaste\s+VERSION\s+([0-9.]+)').Groups[1].Value
    }
}
if ($Version -notmatch '^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$') {
    throw 'Invalid release version.'
}
$executable = Join-Path $BuildDirectory 'snipaste.exe'
$deploy = Join-Path $QtRoot 'bin/windeployqt.exe'
if (-not (Test-Path -LiteralPath $executable) -or -not (Test-Path -LiteralPath $deploy)) {
    throw 'The Release executable or Qt deployment tool was not found.'
}
$dist = [IO.Path]::GetFullPath((Join-Path $repoRoot 'dist'))
$name = "snipaste-$Version-windows-x64"
$stage = [IO.Path]::GetFullPath((Join-Path $dist $name))
$archive = Join-Path $dist "$name.zip"
if ([IO.Path]::GetDirectoryName($stage) -ne $dist) { throw 'Invalid package destination.' }
New-Item -ItemType Directory -Path $dist -Force | Out-Null
if (Test-Path -LiteralPath $stage) { Remove-Item -LiteralPath $stage -Recurse -Force }
if (Test-Path -LiteralPath $archive) { Remove-Item -LiteralPath $archive -Force }
New-Item -ItemType Directory -Path $stage | Out-Null
Copy-Item -LiteralPath $executable -Destination (Join-Path $stage 'snipaste.exe')
$env:Path = (Join-Path $QtRoot 'bin') + ';' + $env:Path
# Qt 6.12 targets Windows 10+, which provides D3DCompiler_47 as an OS component.
& $deploy --release --no-translations --no-opengl-sw --no-system-d3d-compiler --no-compiler-runtime --dir $stage (Join-Path $stage 'snipaste.exe')
if ($LASTEXITCODE -ne 0) { throw 'Qt runtime deployment failed.' }

$redistCandidates = @(
    $env:VCToolsRedistDir,
    'D:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC',
    'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Redist/MSVC'
)
if ($env:VSINSTALLDIR) { $redistCandidates += Join-Path $env:VSINSTALLDIR 'VC/Redist/MSVC' }
$runtime = $null
foreach ($candidate in $redistCandidates) {
    if (-not $candidate -or -not (Test-Path -LiteralPath $candidate)) { continue }
    $runtime = Get-ChildItem -LiteralPath $candidate -Recurse -Directory |
        Where-Object { $_.Name -like 'Microsoft.VC*.CRT' -and $_.Parent.Name -eq 'x64' } |
        Sort-Object FullName | Select-Object -Last 1
    if ($runtime) { break }
}
if (-not $runtime) { throw 'MSVC x64 runtime files were not found.' }
Get-ChildItem -LiteralPath $runtime.FullName -File -Filter '*.dll' | Copy-Item -Destination $stage
"Snipaste $Version`r`nRun snipaste.exe. F1: screenshot. F3: paste." |
    Set-Content -LiteralPath (Join-Path $stage 'README.txt') -Encoding UTF8
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $archive -CompressionLevel Optimal
$hash = (Get-FileHash -LiteralPath $archive -Algorithm SHA256).Hash.ToLowerInvariant()
"$hash *$name.zip" | Set-Content -LiteralPath "$archive.sha256" -Encoding ASCII
Write-Output "Package: $archive"
Write-Output "SHA256: $hash"
