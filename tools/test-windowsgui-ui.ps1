param([Parameter(Mandatory)][string]$Application,
      [Parameter(Mandatory)][string]$TestDirectory,
      [string]$SeedApplication = '')
$ErrorActionPreference = 'Stop'
# Address only this helper process's HWND and stable control IDs. No keyboard or
# mouse injection, focus changes, external application inspection or packages.
Add-Type @"
using System;
using System.Text;
using System.Runtime.InteropServices;
public static class WoodGuiTest {
    [DllImport("user32.dll")] public static extern IntPtr GetDlgItem(IntPtr window, int id);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageW(IntPtr window, uint message, IntPtr w, string text);
    [DllImport("user32.dll", EntryPoint="SendMessageW")] public static extern IntPtr Send(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll", CharSet=CharSet.Unicode)] public static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, IntPtr w, StringBuilder text, uint flags, uint timeout, out IntPtr result);
    [DllImport("user32.dll", EntryPoint="GetWindowLongPtrW")] public static extern IntPtr Style(IntPtr window, int index);
    [DllImport("user32.dll")] public static extern bool PostMessageW(IntPtr window, uint message, IntPtr w, IntPtr l);
    [DllImport("user32.dll")] public static extern bool IsWindow(IntPtr window);
    [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr window);
    public static string Text(IntPtr window) {
        var text = new StringBuilder(1048577);
        IntPtr result;
        if (SendMessageTimeoutW(window, 0xD, new IntPtr(text.Capacity), text, 2, 3000, out result) == IntPtr.Zero)
            throw new InvalidOperationException("Owned control text request timed out");
        return text.ToString();
    }
}
"@
$root = [IO.Path]::GetFullPath($TestDirectory)
if (Test-Path -LiteralPath $root) { throw 'Use a new evidence directory' }
[IO.Directory]::CreateDirectory($root) | Out-Null
$database = Join-Path $root 'notes.sqlite'
$expectedId = 1
if ($SeedApplication -ne '') {
    & $SeedApplication $database | Set-Content -LiteralPath (Join-Path $root 'seed.stdout')
    if ($LASTEXITCODE -ne 0) { throw 'Test database seeding failed' }
    $expectedId = 21
}
$text = "A real Wood note $([char]0xe9) $([char]::ConvertFromUtf32(0x1f333))"
function Wait-For([scriptblock]$Read, [string]$Description) {
    $limit = [DateTime]::UtcNow.AddSeconds(30)
    do {
        $value = & $Read
        if ($value) { return $value }
        Start-Sleep -Milliseconds 50
    } while ([DateTime]::UtcNow -lt $limit)
    throw "Timed out: $Description"
}
for ($round = 1; $round -le 2; $round++) {
    $info = New-Object Diagnostics.ProcessStartInfo
    $info.FileName = [IO.Path]::GetFullPath($Application)
    $info.Arguments = '--database "' + $database + '"'
    $info.UseShellExecute = $false
    $info.CreateNoWindow = $true
    $info.RedirectStandardOutput = $true
    $info.RedirectStandardError = $true
    $info.WorkingDirectory = $root
    $process = [Diagnostics.Process]::Start($info)
    try {
        $window = Wait-For {
            $process.Refresh()
            if ($process.HasExited) { throw "Application exited: $($process.StandardError.ReadToEnd())" }
            if ($process.MainWindowHandle -ne [IntPtr]::Zero) { $process.MainWindowHandle }
        } 'owned application window'
        $entry = Wait-For { $h=[WoodGuiTest]::GetDlgItem($window,2); if($h -ne [IntPtr]::Zero){$h} } 'text input'
        $save = Wait-For { $h=[WoodGuiTest]::GetDlgItem($window,3); if($h -ne [IntPtr]::Zero){$h} } 'save button'
        $status = Wait-For { $h=[WoodGuiTest]::GetDlgItem($window,4); if($h -ne [IntPtr]::Zero){$h} } 'status label'
        $history = Wait-For { $h=[WoodGuiTest]::GetDlgItem($window,5); if($h -ne [IntPtr]::Zero){$h} } 'history view'
        if ($round -eq 1) {
            if ([WoodGuiTest]::SendMessageW($entry,0xC,[IntPtr]::Zero,$text) -eq [IntPtr]::Zero) { throw 'WM_SETTEXT failed' }
            [WoodGuiTest]::Send($save,0xF5,[IntPtr]::Zero,[IntPtr]::Zero) | Out-Null
            Wait-For { [WoodGuiTest]::Text($status) -eq "Saved note #$expectedId" } 'SQLite save acknowledgement' | Out-Null
        }
        Wait-For { [WoodGuiTest]::Text($history).Contains($text) } 'persisted Unicode history' | Out-Null
        if ($SeedApplication -ne '') {
            $view = [WoodGuiTest]::Text($history)
            if ($view.Length -ge 524352 -or !$view.Contains('Earlier notes omitted')) { throw 'Large history was not visibly bounded' }
        }
        if (([WoodGuiTest]::Style($history,-16).ToInt64() -band 0x800) -eq 0) { throw 'History should be readonly' }
        Add-Content -LiteralPath (Join-Path $root 'dpi.txt') ([WoodGuiTest]::GetDpiForWindow($window))
        if (![WoodGuiTest]::PostMessageW($window,0x10,[IntPtr]::Zero,[IntPtr]::Zero)) { throw 'WM_CLOSE failed' }
        if (!$process.WaitForExit(30000)) { throw 'Owned application did not close' }
        [IO.File]::WriteAllText((Join-Path $root "round-$round.stdout"), $process.StandardOutput.ReadToEnd())
        [IO.File]::WriteAllText((Join-Path $root "round-$round.stderr"), $process.StandardError.ReadToEnd())
        if ($process.ExitCode -ne 0) { throw "Application exit $($process.ExitCode)" }
        if ([WoodGuiTest]::IsWindow($window)) { throw 'Window retained after application exit' }
    } finally {
        if (!$process.HasExited) { $process.Kill(); $process.WaitForExit() }
        $process.Dispose()
    }
}
if ((Get-Item -LiteralPath $database).Length -eq 0) { throw 'SQLite database was not persisted' }
Set-Content -LiteralPath (Join-Path $root 'result.txt') 'PASS: native controls, Unicode input, Save invocation, readonly history, SQLite persistence, reopen and normal close'
