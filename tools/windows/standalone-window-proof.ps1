<#
.SYNOPSIS
 Captures initial/maximized/restored Spectr windows and verifies initial client size.
.DESCRIPTION
 Runs in an interactive desktop. Owns only the process it starts, preserving
 existing Spectr/DAW instances. The receipt is geometry evidence, not audio,
 interaction, performance, or installer acceptance. Inspect the PNGs for clipping.
#>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Exe,
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [uint32]$ExpectedClientWidth=990,
 [uint32]$ExpectedClientHeight=645
)
$ErrorActionPreference='Stop'
$Exe=(Resolve-Path -LiteralPath $Exe).Path
$out=$EvidenceDir
$currentSession=(Get-Process -Id $PID).SessionId
if($currentSession -eq 0 -or -not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $currentSession)) {
 throw 'Run from the logged-in Windows desktop session, not SSH Session 0'
}
New-Item -ItemType Directory -Force -Path $out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class Geometry {
 [StructLayout(LayoutKind.Sequential)] public struct Rect {public int L,T,R,B;}
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern uint GetDpiForWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc e,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
}
'@
$process=Start-Process -FilePath $Exe -WorkingDirectory (Split-Path -Parent $Exe) -RedirectStandardOutput "$out\stdout.txt" -RedirectStandardError "$out\stderr.txt" -PassThru
$ownedStart=$process.StartTime
@{desktop=[Windows.Forms.Screen]::PrimaryScreen.Bounds.ToString();pid=$process.Id;start=$ownedStart.ToString('o');exe=$process.Path;sha=(Get-FileHash $process.Path -Algorithm SHA256).Hash} | ConvertTo-Json | Set-Content "$out\process.json"
try {
 for($i=0;$i -lt 30;$i++){Start-Sleep -Milliseconds 500;$process.Refresh();if($process.HasExited){throw "Exited $($process.ExitCode)"};if($process.MainWindowHandle -ne 0){break}}
 $hwnd=$process.MainWindowHandle
 if($hwnd -eq 0){throw 'No window after 15 seconds'}
 foreach($phase in @('initial','maximized','restored')) {
  if($phase -eq 'maximized'){[Geometry]::ShowWindow($hwnd,3)|Out-Null}
  if($phase -eq 'restored'){[Geometry]::ShowWindow($hwnd,9)|Out-Null}
  [Geometry]::SetForegroundWindow($hwnd)|Out-Null
  Start-Sleep -Seconds 3
  [uint32]$foregroundPid=0
  [Geometry]::GetWindowThreadProcessId([Geometry]::GetForegroundWindow(),[ref]$foregroundPid)|Out-Null
  if($foregroundPid -ne $process.Id){throw 'Owned Spectr is not foreground; refusing obscured screenshot'}
  $script:rows=[Collections.Generic.List[object]]::new()
  $collect={param([IntPtr]$h,[IntPtr]$unused)
   $c=New-Object Geometry+Rect;$w=New-Object Geometry+Rect
   [Geometry]::GetClientRect($h,[ref]$c)|Out-Null;[Geometry]::GetWindowRect($h,[ref]$w)|Out-Null
   $name=[Text.StringBuilder]::new(256);[Geometry]::GetClassName($h,$name,256)|Out-Null
   $script:rows.Add(@{hwnd=$h.ToInt64();class=$name.ToString();client=@(($c.R-$c.L),($c.B-$c.T));window=@($w.L,$w.T,$w.R,$w.B);dpi=[Geometry]::GetDpiForWindow($h)})
   return $true
  }
  &$collect $hwnd ([IntPtr]::Zero)|Out-Null
  [Geometry]::EnumChildWindows($hwnd,[Geometry+EnumProc]$collect,[IntPtr]::Zero)|Out-Null
  if($phase -eq 'initial') {
   $actual=$script:rows[0].client
   if($actual[0] -ne $ExpectedClientWidth -or $actual[1] -ne $ExpectedClientHeight) {
    throw "Initial client $($actual -join 'x') differs from expected ${ExpectedClientWidth}x${ExpectedClientHeight}"
   }
  }
  $script:rows | ConvertTo-Json -Depth 6 | Set-Content "$out\$phase.json"
  $r=New-Object Geometry+Rect;[Geometry]::GetWindowRect($hwnd,[ref]$r)|Out-Null
  $bmp=[Drawing.Bitmap]::new($r.R-$r.L,$r.B-$r.T);$g=[Drawing.Graphics]::FromImage($bmp)
  $g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size);$bmp.Save("$out\$phase.png");$g.Dispose();$bmp.Dispose()
 }
 'PASS_INITIAL_SIZE_AND_CAPTURE_ONLY' | Set-Content "$out\status.txt"
} catch {$_ | Out-String | Set-Content "$out\error.txt";exit 1}
finally {$process.Refresh();if(-not $process.HasExited -and $process.StartTime -eq $ownedStart){$process.CloseMainWindow()|Out-Null}}
