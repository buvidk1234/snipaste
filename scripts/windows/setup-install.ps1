param(
    [Parameter(Mandatory = $true, ParameterSetName = 'Install')][string]$Archive,
    [Parameter(Mandatory = $true)][string]$Destination,
    [Parameter(Mandatory = $true, ParameterSetName = 'Close')][switch]$CloseOnly
)

$ErrorActionPreference = 'Stop'
function Close-Application {
    $executable = Join-Path $destinationPath 'snipaste.exe'
    $running = @(Get-Process snipaste -ErrorAction SilentlyContinue | Where-Object { $_.Path -ieq $executable })
    if ($running.Count -gt 0) {
        Start-Process -FilePath $executable -ArgumentList 'app.quit' -WindowStyle Hidden -Wait
        $running | Wait-Process -Timeout 30 -ErrorAction Stop
    }
}

try {
    $destinationPath = [IO.Path]::GetFullPath($Destination)
    if ($destinationPath -eq [IO.Path]::GetPathRoot($destinationPath)) { throw 'Choose an application folder.' }
    if ($CloseOnly) { Close-Application; exit 0 }
    $taskRoot = Join-Path ([IO.Path]::GetTempPath()) ('snipaste-update-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $taskRoot | Out-Null
    $package = Join-Path $taskRoot 'package.zip'
    Copy-Item -LiteralPath $Archive -Destination $package
    $hash = [Security.Cryptography.SHA256]::Create()
    $stream = [IO.File]::OpenRead($package)
    try { $digest = [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $stream.Dispose(); $hash.Dispose() }

    Close-Application
    $manifest = Join-Path $taskRoot 'install.json'
    @{
        archive = $package; destination = $destinationPath; sha256 = $digest
        processId = 0; restart = $false; newInstall = $true; arguments = @()
    } | ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding UTF8
    & (Join-Path $PSScriptRoot 'install-windows.ps1') -Manifest $manifest
    $result = $LASTEXITCODE
    if ($result) {
        $status = Get-Content -LiteralPath (Join-Path $taskRoot 'status.json') -Raw -Encoding UTF8 | ConvertFrom-Json
        throw $status.message
    }
    exit 0
} catch {
    Write-Output $_.Exception.Message
    exit 1
}
