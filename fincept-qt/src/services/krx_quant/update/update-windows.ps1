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
# done. A log goes to %TEMP%\fincept-update.log. When the update does not go in, a message box
# tells the user why (not with -NoLaunch).
#
# The new build's own helper does the swap: this script hands the run over to the copy in the
# ZIP (krx-update\update-windows.ps1) with -Handoff and the parameters above, so a fix here
# reaches an install on its next update. Builds up to krx.42 kept failing with a helper the build
# they downloaded had already fixed (codeyoma/krx-quant#234). Keep accepting these parameters:
# older helpers hand over with them.
#
# The file keeps its UTF-8 BOM: Windows PowerShell 5.1 reads a script without one in the ANSI
# code page, and the message box is Korean.

param(
    [ValidateSet('Swap', 'Move')][string]$Mode = 'Swap',
    [int]$AppPid = 0,
    [string]$Zip,
    [string]$Setup,
    [string]$OldRoot,
    [Parameter(Mandatory = $true)][string]$Root,
    [string]$Relaunch,
    [switch]$NoLaunch,
    [switch]$Handoff
)

$ErrorActionPreference = 'Stop'
$log = Join-Path $env:TEMP 'fincept-update.log'

# Out of the folder this script was started in. The Start menu shortcut opens the app in its
# install folder and a process's working directory keeps that folder in use, so the swap below
# could not move it ("... is in use") while this script stood in it.
[IO.Directory]::SetCurrentDirectory($env:TEMP)
Set-Location -LiteralPath $env:TEMP

# A folder path with backslashes only and no trailing separator.
function Format-Dir([string]$path) {
    return ($path -replace '/', '\').TrimEnd('\')
}

$Root = Format-Dir $Root
$staging = "$Root.staging"
$old = "$Root.old"
# The app's data folder (AppPaths): its Python interpreters live there.
$appData = if ($env:LOCALAPPDATA) { (Format-Dir (Join-Path $env:LOCALAPPDATA 'com.fincept.terminal')) + '\' } else { '' }

# The folder a process stands in (its working directory), read from its PEB: Windows does not
# move a folder a process stands in, and nothing else shows it (codeyoma/krx-quant#234). Empty
# when the reader cannot be built here or the process cannot be read (64-bit processes only).
$cwdReader = $true
try {
    Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
namespace FinceptUpdate {
public static class WorkingDirectory {
    [StructLayout(LayoutKind.Sequential)]
    struct BasicInformation {
        public IntPtr ExitStatus, PebBaseAddress, AffinityMask, BasePriority, UniqueProcessId, ParentId;
    }
    [DllImport("ntdll.dll")]
    static extern int NtQueryInformationProcess(IntPtr process, int infoClass, ref BasicInformation info, int size, out int returned);
    [DllImport("kernel32.dll")] static extern IntPtr OpenProcess(int access, bool inherit, int id);
    [DllImport("kernel32.dll")] static extern bool IsWow64Process(IntPtr process, out bool wow64);
    [DllImport("kernel32.dll")]
    static extern bool ReadProcessMemory(IntPtr process, IntPtr address, byte[] buffer, IntPtr size, out IntPtr read);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    static byte[] Read(IntPtr process, IntPtr address, int size) {
        byte[] buffer = new byte[size];
        IntPtr read;
        return ReadProcessMemory(process, address, buffer, (IntPtr)size, out read) && (long)read == size ? buffer : null;
    }

    // PEB.ProcessParameters at 0x20, then RTL_USER_PROCESS_PARAMETERS.CurrentDirectory at 0x38.
    public static string Of(int id) {
        if (IntPtr.Size != 8) return "";
        IntPtr process = OpenProcess(0x0410, false, id); // PROCESS_QUERY_INFORMATION | PROCESS_VM_READ
        if (process == IntPtr.Zero) return "";
        try {
            bool wow64;
            if (IsWow64Process(process, out wow64) && wow64) return "";
            BasicInformation info = new BasicInformation();
            int returned;
            if (NtQueryInformationProcess(process, 0, ref info, Marshal.SizeOf(info), out returned) != 0) return "";
            byte[] parameters = Read(process, info.PebBaseAddress + 0x20, 8);
            if (parameters == null) return "";
            byte[] path = Read(process, (IntPtr)BitConverter.ToInt64(parameters, 0) + 0x38, 16);
            if (path == null) return "";
            byte[] text = Read(process, (IntPtr)BitConverter.ToInt64(path, 8), BitConverter.ToUInt16(path, 0));
            return text == null ? "" : System.Text.Encoding.Unicode.GetString(text);
        } finally {
            CloseHandle(process);
        }
    }
}
}
'@
} catch {
    $cwdReader = $false
}

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

function Test-Under([string]$path, [string]$prefix) {
    return [bool]($path -and $prefix -and $path.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase))
}

function Get-StandingIn([int]$id) {
    if (-not $cwdReader) { return '' }
    return [FinceptUpdate.WorkingDirectory]::Of($id)
}

# The app's processes that keep <dir> in use: those started from it (QtWebEngineProcess, another
# app window), and its Python, which stands in <dir>\scripts but is started from the data folder.
function Get-ProcessesIn([string]$dir) {
    $prefix = (Format-Dir $dir) + '\'
    @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessId -ne $PID -and ((Test-Under $_.ExecutablePath $prefix) -or
            ((Test-Under $_.ExecutablePath $appData) -and (Test-Under (Get-StandingIn $_.ProcessId) $prefix)))
    })
}

# Every process started from <dir> or standing in it, the app's or not, as "name (pid)": what
# the message names when <dir> cannot move.
function Get-Holders([string]$dir) {
    $prefix = (Format-Dir $dir) + '\'
    $found = @(Get-CimInstance Win32_Process -ErrorAction SilentlyContinue | Where-Object {
        $_.ProcessId -ne $PID -and ((Test-Under $_.ExecutablePath $prefix) -or (Test-Under (Get-StandingIn $_.ProcessId) $prefix))
    } | ForEach-Object { "$($_.Name) ($($_.ProcessId))" })
    return $found -join ', '
}

# Waits up to two minutes for the app's processes in <dir> to exit. The app itself running again
# (the user reopened it) stops the update; any other leftover is stopped.
function Wait-FolderFree([string]$dir) {
    for ($i = 0; $i -lt 240; $i++) {
        if ((Get-ProcessesIn $dir).Count -eq 0) { return }
        Start-Sleep -Milliseconds 500
    }
    $left = Get-ProcessesIn $dir
    if ($left | Where-Object { $_.Name -ieq 'FinceptTerminal.exe' }) { throw "the app is running again from $dir" }
    foreach ($p in $left) {
        Write-Log "stopping $($p.Name) ($($p.ProcessId)), still in $dir"
        Stop-Process -Id $p.ProcessId -Force -ErrorAction SilentlyContinue
    }
    Start-Sleep -Seconds 1
}

# A folder renamed in one step, retried: a virus scanner or the search indexer can hold a file
# for a moment. Not Move-Item: when a process stands in a subfolder, it moves what it can file by
# file, then on a retry moves the folder into the half-made <to>, and the install is left split
# with its exe gone (the old helper did this).
function Move-WithRetry([string]$from, [string]$to) {
    for ($i = 0; ; $i++) {
        try {
            [IO.Directory]::Move($from, $to)
            return
        } catch {
            if ($i -ge 20) { throw $_.Exception.GetBaseException() }
            Start-Sleep -Milliseconds 500
        }
    }
}

# Moves the install folder aside; when Windows says it is in use, the message names what holds it.
function Move-Install([string]$from, [string]$to) {
    try {
        Move-WithRetry $from $to
    } catch {
        $holders = Get-Holders $from
        if (-not $holders) { throw }
        throw "$($_.Exception.Message) In it: $holders."
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
        # In its own folder, as the Start menu shortcut opens it.
        Start-Process -FilePath $exe -WorkingDirectory (Split-Path -Parent $exe)
    }
}

# Tells the user the new build did not go in, and why. Without this the old build just opened
# again, as if the update had worked (codeyoma/krx-quant#234).
function Show-Failure([string]$reason) {
    if ($NoLaunch) { return }
    $build = if ("$Zip$Setup" -match 'custom-v[0-9.]+-krx\.[0-9]+') { $Matches[0] } else { '새 개인 빌드' }
    $text = "$build 업데이트를 넣지 못했습니다. 지금 설치된 빌드를 그대로 씁니다.`n`n" +
            "이유: $reason`n`n" +
            "앱은 다음에 켤 때 새 빌드를 다시 받아 두고, 끌 때 다시 넣어 봅니다.`n기록: $log"
    try {
        # 0x30 warning icon, 0x40000 on top of the app that just opened again.
        (New-Object -ComObject WScript.Shell).Popup($text, 0, 'Fincept Terminal 업데이트', 0x30 + 0x40000) | Out-Null
    } catch {
        Write-Log "could not show the failure: $($_.Exception.Message)"
    }
}

# The Start menu and desktop shortcuts (setup.exe's names, installscript.qs) that open <exe>.
function Get-ShortcutsTo([string]$exe) {
    $found = @()
    try { $shell = New-Object -ComObject WScript.Shell } catch { return $found }
    $places = @(
        (Join-Path ([Environment]::GetFolderPath('Programs')) 'Fincept\Fincept Terminal.lnk'),
        (Join-Path ([Environment]::GetFolderPath('CommonPrograms')) 'Fincept\Fincept Terminal.lnk'),
        (Join-Path ([Environment]::GetFolderPath('Desktop')) 'Fincept Terminal.lnk'),
        (Join-Path ([Environment]::GetFolderPath('CommonDesktopDirectory')) 'Fincept Terminal.lnk'))
    foreach ($place in $places) {
        if (-not (Test-Path -LiteralPath $place)) { continue }
        $link = $shell.CreateShortcut($place)
        if ($link.TargetPath -ieq $exe) {
            $found += [pscustomobject]@{
                Path = $place; Target = $link.TargetPath; WorkingDirectory = $link.WorkingDirectory
                Icon = $link.IconLocation; Description = $link.Description
            }
        }
    }
    return $found
}

function Restore-Shortcuts($shortcuts) {
    if (-not $shortcuts) { return }
    $shell = New-Object -ComObject WScript.Shell
    foreach ($s in $shortcuts) {
        if (Test-Path -LiteralPath $s.Path) { continue }
        try {
            New-Item -ItemType Directory -Force -Path (Split-Path -Parent $s.Path) | Out-Null
            $link = $shell.CreateShortcut($s.Path)
            $link.TargetPath = $s.Target
            $link.WorkingDirectory = $s.WorkingDirectory
            $link.IconLocation = $s.Icon
            $link.Description = $s.Description
            $link.Save()
            Write-Log "made the shortcut $($s.Path) again"
        } catch {
            Write-Log "could not make the shortcut $($s.Path) again: $($_.Exception.Message)"
        }
    }
}

# The helper of the build in the ZIP, written next to this one; none when the ZIP holds none (up
# to this change) or the same script as this one.
function Get-NewHelper {
    Add-Type -AssemblyName System.IO.Compression.FileSystem
    $archive = [System.IO.Compression.ZipFile]::OpenRead($Zip)
    try {
        $entry = $archive.Entries | Where-Object { ($_.FullName -replace '\\', '/') -ieq 'krx-update/update-windows.ps1' } |
            Select-Object -First 1
        if (-not $entry) { return $null }
        $stream = $entry.Open()
        $bytes = New-Object IO.MemoryStream
        try { $stream.CopyTo($bytes) } finally { $stream.Dispose() }
    } finally {
        $archive.Dispose()
    }
    $new = [Convert]::ToBase64String($bytes.ToArray())
    if ($new -eq [Convert]::ToBase64String([IO.File]::ReadAllBytes($PSCommandPath))) { return $null }
    $path = Join-Path $env:TEMP 'update-windows-next.ps1'
    [IO.File]::WriteAllBytes($path, $bytes.ToArray())
    return $path
}

# Runs the swap with the new build's helper and ends with its exit code; returns, to swap with
# this one, when there is none or it cannot be read. That helper reopens the app and tells the
# user on a failure itself.
function Invoke-NewHelper {
    if ($Handoff) { return }
    try {
        $next = Get-NewHelper
    } catch {
        Write-Log "swapping with this helper, the ZIP's could not be read: $($_.Exception.Message)"
        return
    }
    if (-not $next) { return }
    Write-Log "handing over to the new build's helper"
    $arguments = @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $next, '-Handoff',
                   '-Mode', 'Swap', '-AppPid', $AppPid, '-Zip', $Zip, '-Root', $Root)
    if ($Relaunch) { $arguments += @('-Relaunch', $Relaunch) }
    if ($NoLaunch) { $arguments += '-NoLaunch' }
    & (Join-Path $PSHOME 'powershell.exe') @arguments
    $code = $LASTEXITCODE
    Remove-Quietly $next
    exit $code
}

function Update-Swap {
    if (-not $Zip -or -not (Test-Path -LiteralPath $Zip)) { throw "no ZIP: $Zip" }
    Invoke-NewHelper
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
    Move-Install $Root $old
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
    $from = Format-Dir $OldRoot

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

    # The old install goes with its own maintenance tool, which also removes its shortcuts. Those
    # can be the very paths the new install just made (the same Start menu and desktop names), so
    # the new install's shortcuts are noted first and made again if the removal took them.
    $shortcuts = Get-ShortcutsTo $exe
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
    Restore-Shortcuts $shortcuts
    Write-Log "moved to $Root and updated"
    Start-App $exe
}

try {
    Write-Log "update ($Mode) of $Root$(if ($Handoff) { ', by the new build''s helper' })"
    if (-not $cwdReader) { Write-Log 'cannot read what processes stand in; only where they were started from is seen' }
    if ($Mode -eq 'Swap') { Update-Swap } else { Update-Move }
    exit 0
} catch {
    $reason = $_.Exception.Message
    Write-Log "failed: $reason"
    Remove-Quietly $staging
    Remove-Quietly $Zip
    # The user pressed 지금 재시작: open what is installed, so the app does not just vanish.
    if ($Mode -eq 'Swap') { Start-App (Join-Path $Root 'FinceptTerminal.exe') }
    elseif ($OldRoot -and (Test-Path -LiteralPath (Join-Path $OldRoot 'FinceptTerminal.exe'))) {
        Start-App (Join-Path $OldRoot 'FinceptTerminal.exe')
    } else { Start-App (Join-Path $Root 'FinceptTerminal.exe') }
    Show-Failure $reason
    exit 1
}
