# The personal build's in-place update on Windows (codeyoma/krx-quant#92, custom/README.md).
#
# The app downloads a new build's setup.exe, checks its sha256, starts this script and quits.
# This waits for the app to exit, removes the installed build with its maintenance tool and
# installs the new one into the same folder, both headless (the flags the custom-release
# workflow's payload check uses; controlscript.qs advances the wizard pages), then starts it.
# If that cannot be done, it starts the setup wizard so the user can install by hand.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File update-windows.ps1 `
#     -AppPid <pid> -Setup <setup.exe> -Root <install folder> [-NoLaunch]
#
# -NoLaunch (CI): start nothing, neither the new app nor the wizard; the exit code tells.
# setup.exe is removed after a successful install. A log goes to %TEMP%\fincept-update.log.

param(
    [int]$AppPid = 0,
    [Parameter(Mandatory = $true)][string]$Setup,
    [Parameter(Mandatory = $true)][string]$Root,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $env:TEMP 'fincept-update.log'

function Write-Log([string]$message) {
    Add-Content -Path $log -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $message"
}

function Stop-Update([string]$why) {
    Write-Log "failed: $why"
    if (-not $NoLaunch) {
        try { Start-Process -FilePath $Setup } catch { Write-Log "could not start the setup wizard: $($_.Exception.Message)" }
    }
    exit 1
}

# Start-Process joins an argument list with spaces and no quotes: quote paths by hand.
function Invoke-Quiet([string]$file, [string]$arguments) {
    $p = Start-Process -FilePath $file -ArgumentList $arguments -Wait -PassThru
    return $p.ExitCode
}

try {
    Write-Log "update $Root from $Setup"
    if ($AppPid -gt 0) {
        # The app quits right after starting this; give it up to two minutes.
        Wait-Process -Id $AppPid -Timeout 120 -ErrorAction SilentlyContinue
        if (Get-Process -Id $AppPid -ErrorAction SilentlyContinue) { Stop-Update 'the app is still running' }
    }
    $tool = Join-Path $Root 'FinceptMaintenanceTool.exe'
    if (-not (Test-Path $tool)) { Stop-Update "no maintenance tool in $Root" }

    $code = Invoke-Quiet $tool 'purge --default-answer --confirm-command'
    if ($code -ne 0) { Stop-Update "the maintenance tool exited $code" }
    # The installer wants its folder absent or empty. Purge removes it (RemoveTargetDir), the
    # maintenance tool itself a moment after it exits: wait up to 30 seconds.
    for ($i = 0; $i -lt 60 -and (Test-Path $Root) -and (Get-ChildItem -Force $Root | Select-Object -First 1); $i++) {
        Start-Sleep -Milliseconds 500
    }
    if ((Test-Path $Root) -and (Get-ChildItem -Force $Root | Select-Object -First 1)) {
        Stop-Update "$Root is not empty after the purge"
    }

    $code = Invoke-Quiet $Setup "--root `"$Root`" --accept-licenses --default-answer --confirm-command install"
    $exe = Join-Path $Root 'FinceptTerminal.exe'
    if ($code -ne 0 -or -not (Test-Path $exe)) { Stop-Update "the installer exited $code" }

    Remove-Item -Path $Setup -ErrorAction SilentlyContinue
    Write-Log 'updated'
    if (-not $NoLaunch) { Start-Process -FilePath $exe }
    exit 0
} catch {
    Stop-Update $_.Exception.Message
}
