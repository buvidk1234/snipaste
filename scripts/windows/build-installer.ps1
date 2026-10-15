[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$PackageDirectory,
    [Parameter(Mandatory = $true)][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Version,
    [string]$NsisRoot = $env:NSIS_ROOT
)

$ErrorActionPreference = 'Stop'
$compiler = Get-Command makensis.exe -ErrorAction SilentlyContinue | Select-Object -ExpandProperty Source -First 1
if ($NsisRoot) { $compiler = Join-Path $NsisRoot 'makensis.exe' }
if (-not $compiler) {
    foreach ($root in @(${env:ProgramFiles(x86)}, $env:ProgramFiles)) {
        $candidate = Join-Path $root 'NSIS/makensis.exe'
        if (Test-Path -LiteralPath $candidate) { $compiler = $candidate; break }
    }
}
if (-not $compiler -or -not (Test-Path -LiteralPath $compiler)) {
    throw 'NSIS 3 is required to build the EXE installer. Install NSIS or set NSIS_ROOT.'
}
$stage = [IO.Path]::GetFullPath($PackageDirectory)
$archivePath = [IO.Path]::GetFullPath($Archive)
$match = [regex]::Match($Version, '^(\d+)\.(\d+)\.(\d+)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$')
if (-not $match.Success -or -not (Test-Path -LiteralPath (Join-Path $stage 'snipaste.exe')) -or
    -not (Test-Path -LiteralPath $archivePath -PathType Leaf)) { throw 'Invalid installer package.' }
$output = Join-Path ([IO.Path]::GetDirectoryName($archivePath)) ([IO.Path]::GetFileNameWithoutExtension($archivePath) + '-setup.exe')
$fileList = New-TemporaryFile
try {
    $files = @(Get-ChildItem -LiteralPath $stage -File -Recurse)
    $lines = @($files | ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('$', '$$').Replace('"', '$\"')
        'Delete "$INSTDIR\' + $relative + '"'
    })
    $lines += @(Get-ChildItem -LiteralPath $stage -Directory -Recurse | Sort-Object { $_.FullName.Length } -Descending | ForEach-Object {
        $relative = $_.FullName.Substring($stage.Length + 1).Replace('$', '$$').Replace('"', '$\"')
        'RMDir "$INSTDIR\' + $relative + '"'
    })
    $lines | Set-Content -LiteralPath $fileList.FullName -Encoding UTF8
    $productVersion = ($match.Groups[1..3].Value -join '.') + '.0'
    $size = [math]::Ceiling(($files | Measure-Object Length -Sum).Sum / 1024)
    $updateScript = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../updates/install-windows.ps1'))
    & $compiler /INPUTCHARSET UTF8 "/DVERSION=$Version" "/DPRODUCT_VERSION=$productVersion" `
        "/DARCHIVE=$archivePath" "/DOUTPUT_FILE=$output" "/DSCRIPT_DIRECTORY=$PSScriptRoot" `
        "/DUPDATE_SCRIPT=$updateScript" `
        "/DUNINSTALL_FILES=$($fileList.FullName)" "/DINSTALLED_SIZE=$size" (Join-Path $PSScriptRoot 'installer.nsi')
    if ($LASTEXITCODE -ne 0) { throw 'Windows installer compilation failed.' }
} finally {
    Remove-Item -LiteralPath $fileList.FullName -Force
}
$digest = (Get-FileHash -LiteralPath $output -Algorithm SHA256).Hash.ToLowerInvariant()
"$digest *$([IO.Path]::GetFileName($output))" | Set-Content -LiteralPath "$output.sha256" -Encoding ASCII
Write-Output "Installer: $output"
