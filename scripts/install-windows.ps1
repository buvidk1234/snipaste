[CmdletBinding(DefaultParameterSetName = 'Update')]
param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Update')][string]$Manifest,
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')][string]$Archive,
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')]
    [Parameter(Mandatory = $true, ParameterSetName = 'Close')][string]$Destination,
    [Parameter(Mandatory = $true, ParameterSetName = 'Close')][switch]$CloseOnly
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.IO.Compression.FileSystem
if ($Manifest) {
    $settings = Get-Content -LiteralPath $Manifest -Raw -Encoding UTF8 | ConvertFrom-Json
    $taskRoot = [IO.Path]::GetFullPath((Split-Path -Path $Manifest -Parent))
} else {
    try {
        $destinationPath = [IO.Path]::GetFullPath($Destination)
        if ($destinationPath -eq [IO.Path]::GetPathRoot($destinationPath)) { throw 'Choose an application folder.' }
        $executable = Join-Path $destinationPath 'snipaste.exe'
        $running = @(Get-Process snipaste -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $executable })
        if ($running.Count -gt 0) {
            Start-Process -FilePath $executable -ArgumentList 'app.quit' -WindowStyle Hidden -Wait
            $running | Wait-Process -Timeout 30 -ErrorAction Stop
        }
        if ($CloseOnly) { exit 0 }
        $taskRoot = Join-Path ([IO.Path]::GetTempPath()) ('snipaste-update-' + [Guid]::NewGuid().ToString('N'))
        New-Item -ItemType Directory -Path $taskRoot | Out-Null
        $package = Join-Path $taskRoot 'package.zip'
        Copy-Item -LiteralPath $Archive -Destination $package
        $settings = [pscustomobject]@{
            archive = $package; destination = $destinationPath
            processId = 0; restart = $false; newInstall = $true; arguments = @()
        }
    } catch {
        Write-Output $_.Exception.Message
        exit 1
    }
}
$destination = [IO.Path]::GetFullPath($settings.destination)
$archive = [IO.Path]::GetFullPath($settings.archive)
$staging = Join-Path $taskRoot 'expanded'
$backup = Join-Path $taskRoot 'backup'
$statusFile = Join-Path $taskRoot 'status.json'
$changed = New-Object 'System.Collections.Generic.List[object]'
$success = $false

function Write-Status([string]$state, [string]$message = '') {
    @{ state = $state; message = $message } | ConvertTo-Json |
        Set-Content -LiteralPath $statusFile -Encoding UTF8
}

function Quote-Argument([string]$value) {
    $quoted = [regex]::Replace($value, '(\\*)"', '$1$1\"')
    $quoted = [regex]::Replace($quoted, '(\\+)$', '$1$1')
    return '"' + $quoted + '"'
}

try {
    if (-not $taskRoot.StartsWith([IO.Path]::GetFullPath([IO.Path]::GetTempPath()), [StringComparison]::OrdinalIgnoreCase) -or
        -not (Split-Path -Path $taskRoot -Leaf).StartsWith('snipaste-update-') -or
        -not $archive.StartsWith($taskRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
        $destination -eq [IO.Path]::GetPathRoot($destination) -or
        (-not $settings.newInstall -and
         -not (Test-Path -LiteralPath (Join-Path $destination 'snipaste.exe') -PathType Leaf))) {
        throw 'Invalid update paths.'
    }
    $sha256 = [Security.Cryptography.SHA256]::Create()
    try {
        $archiveStream = [IO.File]::OpenRead($archive)
        try {
            $archiveHash = [BitConverter]::ToString($sha256.ComputeHash($archiveStream)).Replace('-', '').ToLowerInvariant()
        } finally {
            $archiveStream.Dispose()
        }
    } finally {
        $sha256.Dispose()
    }
    if ($Manifest -and $archiveHash -ne $settings.sha256) {
        throw 'The downloaded archive checksum has changed.'
    }
    $isSetup = [IO.Path]::GetExtension($archive) -ieq '.exe'
    if (-not $isSetup) {
        New-Item -ItemType Directory -Path $staging -Force | Out-Null
        $staging = [IO.Path]::GetFullPath($staging)
        $zip = [IO.Compression.ZipFile]::OpenRead($archive)
        try {
            foreach ($entry in $zip.Entries) {
                $entryTarget = [IO.Path]::GetFullPath((Join-Path $staging $entry.FullName))
                if (-not $entryTarget.StartsWith($staging + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase) -or
                    $entry.FullName -match '(^|[/\\])\.\.([/\\]|$)' -or
                    $entry.FullName -match ':') {
                    throw 'The update archive contains an unsafe path.'
                }
            }
        } finally {
            $zip.Dispose()
        }
        [IO.Compression.ZipFile]::ExtractToDirectory($archive, $staging)
        if (-not (Test-Path -LiteralPath (Join-Path $staging 'snipaste.exe') -PathType Leaf) -or
            -not (Test-Path -LiteralPath (Join-Path $staging 'Qt6Core.dll') -PathType Leaf)) {
            throw 'The update archive is not a Windows portable package.'
        }
    }
    Write-Status 'ready'
    # Wait by polling so the helper also works when the parent exits before it starts.
    $deadline = [DateTime]::UtcNow.AddSeconds(120)
    while ($settings.processId -gt 0 -and (Get-Process -Id ([int]$settings.processId) -ErrorAction SilentlyContinue)) {
        if ([DateTime]::UtcNow -gt $deadline) { throw 'The application did not exit in time.' }
        Start-Sleep -Milliseconds 250
    }
    if ($isSetup) {
        $setup = New-Object Diagnostics.ProcessStartInfo
        $setup.FileName = $archive
        # The EXE installs into Program Files and requests administrator access.
        # Keep this helper unelevated so the application restarts as its user.
        $setup.UseShellExecute = $true
        $setup.Verb = 'runas'
        $setup.WindowStyle = [Diagnostics.ProcessWindowStyle]::Hidden
        # NSIS requires /D to be last and its path must not be quoted.
        $setup.Arguments = '/S ' + $(if ($settings.portable) { '/PORTABLE ' } else { '' }) + '/D=' + $destination
        $process = [Diagnostics.Process]::Start($setup)
        $process.WaitForExit()
        if ($process.ExitCode -ne 0) { throw "Windows setup failed (exit $($process.ExitCode))." }
    } else {
        foreach ($file in Get-ChildItem -LiteralPath $staging -Recurse -File) {
            $relative = $file.FullName.Substring($staging.Length + 1)
            $target = [IO.Path]::GetFullPath((Join-Path $destination $relative))
            if (-not $target.StartsWith($destination + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
                throw 'The installation target is outside the application folder.'
            }
            $saved = Join-Path $backup $relative
            $existed = Test-Path -LiteralPath $target -PathType Leaf
            if ($existed) {
                New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($saved)) -Force | Out-Null
                Copy-Item -LiteralPath $target -Destination $saved
            }
            $changed.Add(@{ target = $target; saved = $saved; existed = $existed })
            New-Item -ItemType Directory -Path ([IO.Path]::GetDirectoryName($target)) -Force | Out-Null
            Copy-Item -LiteralPath $file.FullName -Destination $target -Force
        }
    }
    $success = $true
    Write-Status 'installed'
} catch {
    $problem = $_.Exception.Message
    for ($index = $changed.Count - 1; $index -ge 0; $index--) {
        $item = $changed[$index]
        try {
            if ($item.existed) { Copy-Item -LiteralPath $item.saved -Destination $item.target -Force }
            elseif (Test-Path -LiteralPath $item.target) { Remove-Item -LiteralPath $item.target -Force }
        } catch { $problem += ' Rollback failed: ' + $_.Exception.Message }
    }
    Write-Status 'failed' $problem
}

if ($settings.restart -and (Test-Path -LiteralPath (Join-Path $destination 'snipaste.exe'))) {
    $start = New-Object Diagnostics.ProcessStartInfo
    $start.FileName = Join-Path $destination 'snipaste.exe'
    $start.WorkingDirectory = $destination
    $start.UseShellExecute = $false
    $start.EnvironmentVariables.Remove('QT_PLUGIN_PATH')
    $start.EnvironmentVariables.Remove('QT_QPA_PLATFORM_PLUGIN_PATH')
    $start.Arguments = ($settings.arguments | ForEach-Object { Quote-Argument ([string]$_) }) -join ' '
    [Diagnostics.Process]::Start($start) | Out-Null
}
if (-not $success) { exit 1 }
