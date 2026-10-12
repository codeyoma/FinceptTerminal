# The personal build's self-update on Windows (codeyoma/krx-quant#92, #130, custom/README.md
# "앱 업데이트").
#
# The app downloads the new build, checks its sha256, starts this script and keeps running. The
# script prepares what it can while the app runs, waits for the app to exit (the user pressed
# 지금 재시작, or quit the app some other time), puts the new build in place, and reopens it only
# when the -Relaunch file exists by then (the app makes it for 지금 재시작).
#
#   -Mode Swap: the build is installed in the user's folder. The ZIP (the installed tree without
#     the installer's own files) is unpacked next to it as <Root>.staging while the app runs.
#     After the app exits, <Root> moves aside to <Root>.old and the new tree takes its place;
#     anything the new tree lacks (the maintenance tool and its files) is copied over from the old
#     one. On any failure the old folder goes back. No administrator rights are needed.
#   -Mode Move: the build is installed elsewhere (Program Files). After the app exits, the new
#     setup.exe installs headless into <Root>, the user's folder, and the old install at
#     <OldRoot> is removed with its maintenance tool: elevated (one UAC prompt) when that folder
#     is not writable. If the user declines, the old install stays and the new one still runs.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File update-windows.ps1 -AppPid <pid> `
#     -Mode Swap -Zip <zip> -Root <install folder> [-Relaunch <file>] [-NoLaunch]
#   powershell ... -Mode Move -Setup <setup.exe> -OldRoot <install folder> -Root <user folder> ...
#
# -NoLaunch (CI): never start the app; the exit code tells. The downloaded file is removed when
# done. A log goes to %TEMP%\fincept-update.log.

param(
    [ValidateSet('Swap', 'Move')][string]$Mode = 'Swap',
    [int]$AppPid = 0,
    [string]$Zip,
    [string]$Setup,
    [string]$OldRoot,
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Relaunch,
    [switch]$NoLaunch
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $env:TEMP 'fincept-update.log'
$Root = $Root.TrimEnd('\')
$staging = "$Root.staging"
$old = "$Root.old"

function Write-Log([string]$message) {
    Add-Content -Path $log -Value "$(Get-Date -Format 'yyyy-MM-dd HH:mm:ss') $message"
}

function Remove-Quietly([string]$path) {
    if ($path -and (Test-Path -LiteralPath $path)) {
        Remove-Item -LiteralPath $path -Recurse -Force -ErrorAction SilentlyContinue
    }
}

function Wait-App {
    if ($AppPid -gt 0) {
        Write-Log "waiting for the app (pid $AppPid) to exit"
        Wait-Process -Id $AppPid -ErrorAction SilentlyContinue
    }
}

# Processes started from <dir> (QtWebEngineProcess, another app window) keep its files open.
function Get-ProcessesIn([string]$dir) {
    $prefix = $dir.TrimEnd('\') + '\'
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ExecutablePath -and $_.ExecutablePath.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase)
    })
}

# Waits up to two minutes for everything started from <dir> to exit. The app itself running again
# (the user reopened it) stops the update; any other leftover is stopped.
function Wait-FolderFree([string]$dir) {
    for ($i = 0; $i -lt 240; $i++) {
        if ((Get-ProcessesIn $dir).Count -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    $left = Get-ProcessesIn $dir
    if ($left | Where-Object { $_.Name -ieq 'FinceptTerminal.exe' }) { throw "the app is running again from $dir" }
    foreach ($p in $left) {
        Write-Log "stopping $($p.Name) ($($p.ProcessId)), still running from $dir"
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 1
}

# Move-Item, retried: a virus scanner or the search indexer can hold a file for a moment.
function Move-WithRetry([string]$from, [string]$to) {
    for ($i = 0; ; $i++) {
        try {
            Move-Item -LiteralPath $from -Destination $to
            return
        } catch {
            if ($i -ge 20) { throw }
            Start-Sleep -Milliseconds 500
        }
    }
}

function Test-Writable([string]$dir) {
    $probe = Join-Path $dir ".fincept-write-test-$PID"
    try {
        [IO.File]::WriteAllText($probe, '')
        Remove-Item -LiteralPath $probe -Force
        return $true
    } catch {
        return $false
    }
}

# Reopens the app when the user asked for a restart, the new build or, after a failure, the old one.
function Start-App([string]$exe) {
    if ($NoLaunch -or -not $Relaunch -or -not (Test-Path -LiteralPath $Relaunch)) { return }
    Remove-Quietly $Relaunch
    if ($exe -and (Test-Path -LiteralPath $exe)) {
        Write-Log "opening $exe"
        Start-Process -FilePath $exe
    }
}

function Update-Swap {
    if (-not $Zip -or -not (Test-Path -LiteralPath $Zip)) { throw "no ZIP: $Zip" }
    Remove-Quietly $staging # left over from an earlier run
    Remove-Quietly $old
    Write-Log "unpacking $Zip to $staging"
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    [System.IO.Compression.ZipFile]::ExtractToDirectory($Zip, $staging)
    if (-not (Test-Path -LiteralPath (Join-Path $staging 'FinceptTerminal.exe'))) {
        throw 'the ZIP holds no FinceptTerminal.exe'
    }

    Wait-App
    Wait-FolderFree $Root
    Move-WithRetry $Root $old
    try {
        Move-WithRetry $staging $Root
        # Copied, not moved, so the old folder stays whole until the swap has worked.
        foreach ($item in Get-ChildItem -LiteralPath $old -Force) {
            if (-not (Test-Path -LiteralPath (Join-Path $Root $item.Name))) {
                Copy-Item -LiteralPath $item.FullName -Destination $Root -Recurse -Force
            }
        }
    } catch {
        Write-Log "putting the old install back: $($_.Exception.Message)"
        if (Test-Path -LiteralPath $Root) {
            Move-WithRetry $Root "$staging.failed"
            Remove-Quietly "$staging.failed"
        }
        Move-WithRetry $old $Root
        throw
    }
    Remove-Quietly $old
    Remove-Quietly $Zip
    Write-Log 'updated'
    Start-App (Join-Path $Root 'FinceptTerminal.exe')
}

function Update-Move {
    if (-not $Setup -or -not (Test-Path -LiteralPath $Setup)) { throw "no setup.exe: $Setup" }
    if (-not $OldRoot) { throw 'no -OldRoot' }
    $from = $OldRoot.TrimEnd('\')

    Wait-App
    Wait-FolderFree $from
    # The installer wants its folder absent or empty: anything already there moves aside.
    $aside = $false
    if ((Test-Path -LiteralPath $Root) -and (Get-ChildItem -LiteralPath $Root -Force | Select-Object -First 1)) {
        Remove-Quietly $old
        Move-WithRetry $Root $old
        $aside = $true
    }
    Write-Log "installing $Setup into $Root"
    $p = Start-Process -FilePath $Setup -Wait -PassThru `
        -ArgumentList "--root `"$Root`" --accept-licenses --default-answer --confirm-command install"
    $exe = Join-Path $Root 'FinceptTerminal.exe'
    if ($p.ExitCode -ne 0 -or -not (Test-Path -LiteralPath $exe)) {
        Remove-Quietly $Root
        if ($aside) { Move-WithRetry $old $Root }
        throw "the installer exited $($p.ExitCode)"
    }
    if ($aside) { Remove-Quietly $old }
    Remove-Quietly $Setup

    # The old install goes with its own maintenance tool, which also removes its shortcuts.
    $tool = Join-Path $from 'FinceptMaintenanceTool.exe'
    if (Test-Path -LiteralPath $tool) {
        $elevate = @{}
        if (-not (Test-Writable $from)) { $elevate = @{ Verb = 'RunAs' } }
        try {
            $q = Start-Process -FilePath $tool -ArgumentList 'purge --default-answer --confirm-command' -Wait -PassThru @elevate
            Write-Log "removed the old install at $from (exit $($q.ExitCode))"
        } catch {
            Write-Log "the old install at $from stays: $($_.Exception.Message)"
        }
    }
    Write-Log "moved to $Root and updated"
    Start-App $exe
}

try {
    Write-Log "update ($Mode) of $Root"
    if ($Mode -eq 'Swap') { Update-Swap } else { Update-Move }
    exit 0
} catch {
    Write-Log "failed: $($_.Exception.Message)"
    Remove-Quietly $staging
    Remove-Quietly $Zip
    # The user pressed 지금 재시작: open what is installed, so the app does not just vanish.
    if ($Mode -eq 'Swap') { Start-App (Join-Path $Root 'FinceptTerminal.exe') }
    elseif ($OldRoot -and (Test-Path -LiteralPath (Join-Path $OldRoot 'FinceptTerminal.exe'))) {
        Start-App (Join-Path $OldRoot 'FinceptTerminal.exe')
    } else { Start-App (Join-Path $Root 'FinceptTerminal.exe') }
    exit 1
}
