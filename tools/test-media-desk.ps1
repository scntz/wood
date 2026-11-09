param([Parameter(Mandatory)][string]$Application,
      [Parameter(Mandatory)][string]$TestDirectory)
$ErrorActionPreference = 'Stop'
# Drives the real application through its own window handles and stable control IDs. No
# keyboard or mouse injection and no focus changes. Audio plays at volume zero.
Add-Type @"
using System;
using System.Text;
using System.Collections.Generic;
using System.Runtime.InteropServices;
public static class Desk {
    public delegate bool Visit(IntPtr window, IntPtr data);
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr w, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr w, StringBuilder text, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] public static extern IntPtr GetMenu(IntPtr window);
    [DllImport("user32.dll")] public static extern int GetMenuItemCount(IntPtr menu);
    [DllImport("user32.dll")] public static extern bool EnumThreadWindows(uint thread, Visit visit, IntPtr data);
    [DllImport("user32.dll", SetLastError=true)] public static extern uint GetWindowThreadProcessId(IntPtr window, out uint process);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern int GetClassNameW(IntPtr window, StringBuilder text, int count);
    public static string Text(IntPtr window) {
        var text = new StringBuilder(65536); IntPtr result;
        if (SendMessageTimeoutW(window, 0xD, new IntPtr(text.Capacity), text, 2, 5000, out result) == IntPtr.Zero)
            throw new InvalidOperationException("Control text request timed out");
        return text.ToString();
    }
    public static IntPtr Dialog(IntPtr main) {
        uint process; uint thread = GetWindowThreadProcessId(main, out process);
        IntPtr found = IntPtr.Zero;
        EnumThreadWindows(thread, (w, d) => {
            var name = new StringBuilder(32); GetClassNameW(w, name, 32);
            if (name.ToString() == "#32770" && IsWindowVisible(w)) { found = w; return false; }
            return true;
        }, IntPtr.Zero);
        return found;
    }
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr window, IntPtr device, uint flags);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int left, top, right, bottom; }
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr window, out RECT area);
    [StructLayout(LayoutKind.Sequential)] public struct BITMAPINFOHEADER { public int size, width, height; public short planes, bitCount; public int compression, imageSize, xPels, yPels, used, important; }
    [DllImport("user32.dll")] public static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] public static extern int ReleaseDC(IntPtr window, IntPtr device);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateCompatibleDC(IntPtr device);
    [DllImport("gdi32.dll")] public static extern bool DeleteDC(IntPtr device);
    [DllImport("gdi32.dll")] public static extern bool DeleteObject(IntPtr handle);
    [DllImport("gdi32.dll")] public static extern IntPtr SelectObject(IntPtr device, IntPtr item);
    [DllImport("gdi32.dll")] public static extern IntPtr CreateDIBSection(IntPtr device, ref BITMAPINFOHEADER info, uint usage, out IntPtr bits, IntPtr section, uint offset);
    // Captures what the control actually draws and counts pixels near each RGB color.
    public static int[] Colors(IntPtr control, int[][] wanted) {
        RECT area; if (!GetWindowRect(control, out area)) throw new InvalidOperationException("No control area");
        int width = area.right - area.left, height = area.bottom - area.top;
        var counts = new int[wanted.Length];
        IntPtr screen = GetDC(IntPtr.Zero), memory = CreateCompatibleDC(screen), bits;
        var info = new BITMAPINFOHEADER { size = 40, width = width, height = -height, planes = 1, bitCount = 32 };
        IntPtr bitmap = CreateDIBSection(memory, ref info, 0, out bits, IntPtr.Zero, 0);
        SelectObject(memory, bitmap);
        bool ok = PrintWindow(control, memory, 2);
        if (ok) for (int y = 0; y < height; y += 2) for (int x = 0; x < width; x += 2) {
            int value = Marshal.ReadInt32(bits, (y*width+x)*4);
            int r = (value>>16)&255, g = (value>>8)&255, b = value&255;
            for (int i = 0; i < wanted.Length; ++i)
                if (Math.Abs(r-wanted[i][0]) < 6 && Math.Abs(g-wanted[i][1]) < 6 && Math.Abs(b-wanted[i][2]) < 6) counts[i]++;
        }
        DeleteObject(bitmap); DeleteDC(memory); ReleaseDC(IntPtr.Zero, screen);
        if (!ok) throw new InvalidOperationException("Capture failed");
        return counts;
    }
}
"@
$WM_SETTEXT=0xC; $WM_CLOSE=0x10; $WM_COMMAND=0x111; $BM_CLICK=0xF5; $BM_GETCHECK=0xF0
$LB_GETCOUNT=0x18B; $LB_SETCURSEL=0x186; $LB_FINDSTRING=0x18F; $STM_GETIMAGE=0x173; $LBN_SELCHANGE=1
$root = [IO.Path]::GetFullPath($TestDirectory)
if (Test-Path -LiteralPath $root) { throw 'Use a new evidence directory' }
[IO.Directory]::CreateDirectory($root) | Out-Null
$checks = 0
$script:launched = @()
# A failed check must not leave the application running.
trap { foreach ($running in $script:launched) { if (-not $running.HasExited) { $running.Kill() } }; throw $_ }
function Pass([string]$what) { $script:checks++; Add-Content (Join-Path $root 'checks.txt') "PASS: $what"; Write-Output "PASS: $what" }
function Wait-For([scriptblock]$Read, [string]$Description) {
    $limit = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $value = & $Read
        if ($value) { return $value }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $limit)
    $shown = ""; if ($script:current) { try { $shown = [Desk]::Text([Desk]::GetDlgItem($script:current, 10)) } catch {} }
    throw "Timed out: $Description. Status: $shown"
}
# Distribution check: the application runs from a fresh folder, away from the source tree.
$installed = Join-Path $root 'installed app'
[IO.Directory]::CreateDirectory($installed) | Out-Null
$exe = Join-Path $installed 'media-desk.exe'
Copy-Item -LiteralPath $Application -Destination $exe
$samples = Join-Path $root 'samples'
$work = Join-Path $root 'working'
[IO.Directory]::CreateDirectory($work) | Out-Null
& $exe --make-samples $samples | Out-Null
if ($LASTEXITCODE -ne 0) { throw 'Sample creation failed' }
Pass 'sample PNG, BMP and WAV files created by the application itself'
$database = Join-Path $root 'library.sqlite'
$log = Join-Path $root 'media-desk.log'
function Start-App {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = $exe; $info.WorkingDirectory = $work; $info.UseShellExecute = $false
    $info.RedirectStandardError = $true
    $info.ArgumentList.Add('--database'); $info.ArgumentList.Add($database)
    $info.ArgumentList.Add('--log'); $info.ArgumentList.Add($log)
    $info.ArgumentList.Add('--volume'); $info.ArgumentList.Add('0.0')
    $info.Environment['PATH'] = "$env:SystemRoot\System32"
    $process = [Diagnostics.Process]::Start($info)
    $process.BeginErrorReadLine()
    $script:launched += $process
    $window = Wait-For { $process.Refresh(); if ($process.HasExited) { throw "Application exited with $($process.ExitCode)" }; $h = $process.MainWindowHandle; if ($h -ne [IntPtr]::Zero) { $h } } 'application window'
    return @($process, $window)
}
function Control([IntPtr]$window, [int]$id) { $h = [Desk]::GetDlgItem($window, $id); if ($h -eq [IntPtr]::Zero) { throw "Missing control $id" }; return $h }
function Click([IntPtr]$window, [int]$id) { [Desk]::PostMessageW((Control $window $id), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null }
function Status([IntPtr]$window) { [Desk]::Text((Control $window 10)) }
function Choose([IntPtr]$window, [string]$fragment) {
    $list = Control $window 5
    $count = [int][Desk]::Send($list, $LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero)
    $row = [int][Desk]::SendMessageW($list, $LB_FINDSTRING, [IntPtr](-1), (Join-Path $samples $fragment))
    if ($row -lt 0) { throw "List has no $fragment among $count rows" }
    [Desk]::Send($list, $LB_SETCURSEL, [IntPtr]$row, [IntPtr]::Zero) | Out-Null
    [Desk]::PostMessageW($window, $WM_COMMAND, [IntPtr](($LBN_SELCHANGE -shl 16) -bor 5), $list) | Out-Null
}
$first = Start-App; $process = $first[0]; [IntPtr]$window = $first[1]; $script:current = $window
Wait-For { (Status $window) -like '0 item*' } 'empty library message' | Out-Null
Pass 'application opens with an empty library, without Wood or Python on PATH'
$menu = [Desk]::GetMenu($window)
if ($menu -eq [IntPtr]::Zero -or [Desk]::GetMenuItemCount($menu) -ne 2) { throw 'Menu bar is missing' }
Pass 'menu bar has File and Help'
# An invalid folder is an error in the window, not a crash.
[Desk]::SendMessageW((Control $window 2), $WM_SETTEXT, [IntPtr]::Zero, (Join-Path $root 'does-not-exist')) | Out-Null
Click $window 3
Wait-For { (Status $window) -like 'Error: That folder does not exist*' } 'missing folder error' | Out-Null
Pass 'missing folder reported in the window'
[Desk]::SendMessageW((Control $window 2), $WM_SETTEXT, [IntPtr]::Zero, $samples) | Out-Null
Click $window 3
Wait-For { (Status $window) -like 'Scan found 4 new*' } 'background scan result' | Out-Null
$list = Control $window 5
if ([int][Desk]::Send($list, $LB_GETCOUNT, [IntPtr]::Zero, [IntPtr]::Zero) -ne 4) { throw 'Expected 4 rows' }
Pass 'background task scanned the folder and the list shows 4 items'
Choose $window 'sample-card.png'
Wait-For { (Status $window) -like 'Showing sample-card.png at 320 x 200*' } 'PNG preview' | Out-Null
if ([Desk]::Send((Control $window 6), $STM_GETIMAGE, [IntPtr]::Zero, [IntPtr]::Zero) -eq [IntPtr]::Zero) { throw 'Image control is empty' }
$brown = [int[]](120,78,44); $green = [int[]](46,110,80); $paper = [int[]](244,241,234)
$seen = [Desk]::Colors($window, [int[][]]@($brown,$green,$paper))
if ($seen[0] -lt 500 -or $seen[1] -lt 500 -or $seen[2] -lt 500) { throw "Image control does not show the picture: $($seen -join ',')" }
Pass "PNG decoded and drawn on screen: rectangle, circle and background colors all present ($($seen -join ', ') sampled pixels)"
Choose $window 'sample-gray.bmp'
Wait-For { (Status $window) -like 'Showing sample-gray.bmp*' } 'BMP preview' | Out-Null
Pass 'BMP decoded and displayed'
Choose $window 'broken.png'
Wait-For { (Status $window) -like 'Error:*' } 'broken image error' | Out-Null
if (-not [Desk]::IsWindow($window)) { throw 'Window closed after a bad file' }
Pass 'corrupt image reported in the window and the application keeps running'
Choose $window 'sample-chord.wav'
Wait-For { (Status $window) -like 'sample-chord.wav: 22050 Hz*' } 'WAV details' | Out-Null
Pass 'WAV file read and its format shown'
Click $window 7
Wait-For { (Status $window) -like 'Finished playing*' } 'playback completion' | Out-Null
Pass 'audio played on the real output device from a background task, at volume zero'
Choose $window 'broken.png'
Wait-For { (Status $window) -like 'Error:*' } 'select broken row' | Out-Null
Click $window 8
Wait-For { (Status $window) -like 'Removed; 3 in your library*' } 'remove row' | Out-Null
Pass 'item removed from the library'
# Native dialogs, reached through the real menu item.
[Desk]::PostMessageW($window, $WM_COMMAND, [IntPtr]103, [IntPtr]::Zero) | Out-Null
$dialog = Wait-For { $d = [Desk]::Dialog($window); if ($d -ne [IntPtr]::Zero) { $d } } 'About dialog'
[Desk]::PostMessageW((Control $dialog 2), $BM_CLICK, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
Wait-For { [Desk]::Dialog($window) -eq [IntPtr]::Zero } 'About dialog closing' | Out-Null
Pass 'menu command opened a native message box'
$extra = Join-Path $root 'chosen-through-dialog.png'
Copy-Item -LiteralPath (Join-Path $samples 'sample-card.png') -Destination $extra
[Desk]::PostMessageW($window, $WM_COMMAND, [IntPtr]101, [IntPtr]::Zero) | Out-Null
$dialog = Wait-For { $d = [Desk]::Dialog($window); if ($d -ne [IntPtr]::Zero) { $d } } 'file dialog'
$edit = Wait-For { $h = [Desk]::GetDlgItem($dialog, 0x47C); if ($h -ne [IntPtr]::Zero) { $h } } 'file name box'
$field = $edit
$class = New-Object Text.StringBuilder 32; [Desk]::GetClassNameW($edit, $class, 32) | Out-Null
if ($class.ToString() -eq 'ComboBoxEx32') { $field = [IntPtr]::Zero; $combo = [Desk]::GetDlgItem($edit, 0x3E9); if ($combo -ne [IntPtr]::Zero) { $field = $combo } }
if ($field -eq [IntPtr]::Zero) { $field = $edit }
[Desk]::SendMessageW($field, $WM_SETTEXT, [IntPtr]::Zero, $extra) | Out-Null
[Desk]::PostMessageW($dialog, $WM_COMMAND, [IntPtr]1, [IntPtr]::Zero) | Out-Null
Wait-For { (Status $window) -like 'Added chosen-through-dialog.png*' } 'file dialog result' | Out-Null
Pass 'native file dialog returned a file and it joined the library'
[Desk]::Send($window, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
if (-not $process.WaitForExit(30000)) { throw 'Application did not close' }
if ($process.ExitCode -ne 0) { throw "Application exit code $($process.ExitCode)" }
Pass 'application closed normally with exit code 0'
# Everything must still be there after a restart.
$second = Start-App; $process = $second[0]; [IntPtr]$window = $second[1]; $script:current = $window
Wait-For { (Status $window) -like '4 item*' } 'persistent library' | Out-Null
if ([Desk]::Text((Control $window 2)) -ne $samples) { throw 'Folder setting was not persisted' }
if ([int][Desk]::Send((Control $window 9), $BM_GETCHECK, [IntPtr]::Zero, [IntPtr]::Zero) -ne 1) { throw 'Check box setting was not persisted' }
Pass 'library and settings survived a restart in SQLite'
[Desk]::Send($window, $WM_CLOSE, [IntPtr]::Zero, [IntPtr]::Zero) | Out-Null
if (-not $process.WaitForExit(30000) -or $process.ExitCode -ne 0) { throw 'Second close failed' }
$text = [IO.File]::ReadAllText($log)
if ($text -notlike '*info media-desk: started*' -or $text -notlike '*warn media-desk:*') { throw 'Log file is incomplete' }
Pass 'log file records normal events and warnings'
Set-Content (Join-Path $root 'result.txt') "PASS: $checks checks"
Write-Output "PASS: $checks checks"
