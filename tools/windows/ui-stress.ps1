<#
.SYNOPSIS
 Exercises the real standalone HWND with live audio and 64-band modulation.
.DESCRIPTION
 Run from the logged-in desktop. Trace captures deliberately omit screenshots,
 layout dumps and per-tick state files. Functional captures retain those outside
 the timed profiling pass. A completed run is evidence to review, not a product
 acceptance verdict. The caller must also test installed VST3 and CLAP payloads.
#>
[CmdletBinding()]
param(
 [Parameter(Mandatory=$true)][string]$Exe,
 [Parameter(Mandatory=$true)][string]$EvidenceDir,
 [switch]$Trace,
 [ValidateSet('full','gesture','settings','presets','resize')][string]$Scenario='full',
 [ValidateRange(15,120)][int]$TraceSeconds=65
)
$ErrorActionPreference='Stop'
$Exe=(Resolve-Path -LiteralPath $Exe).Path
$out=[IO.Path]::GetFullPath($EvidenceDir)
if(Test-Path -LiteralPath $out){throw 'Use a fresh evidence directory; stale files cannot establish a result'}
$session=(Get-Process -Id $PID).SessionId
if($session -eq 0 -or -not (Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq $session)){
 throw 'Run in the logged-in desktop session, not SSH Session 0'
}
New-Item -ItemType Directory -Path $out | Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type @'
using System; using System.Text; using System.Runtime.InteropServices;
public static class StressWindow {
 [StructLayout(LayoutKind.Sequential)] public struct Rect { public int L,T,R,B; }
 public delegate bool EnumProc(IntPtr h,IntPtr p);
 [DllImport("user32.dll")] public static extern bool GetClientRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h,out Rect r);
 [DllImport("user32.dll")] public static extern bool EnumChildWindows(IntPtr h,EnumProc e,IntPtr p);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)] public static extern int GetClassName(IntPtr h,StringBuilder b,int n);
 [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h,out uint p);
 [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
 [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr h,int n);
 [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
 [DllImport("user32.dll")] public static extern IntPtr SendMessageTimeout(IntPtr h,uint m,IntPtr w,IntPtr l,uint f,uint t,out IntPtr r);
}
'@
# No external harness inherited from a previous capture may alter this workload.
$names=@('PULP_AUDIO_DEVICE','PULP_SCREENSHOT','PULP_FRAMES','PULP_HEADLESS','PULP_TEST_MODE',
 'SPECTR_MENU_SCENARIO','SPECTR_MENU_SCENARIO_OUT','SPECTR_LAYOUT_DUMP','SPECTR_STATE_TRACE',
 'SPECTR_STATE_OUT','SPECTR_MODULATION_DUMP','SPECTR_GESTURE_PERF','SPECTR_CLICK','SPECTR_OPEN_SETTINGS',
 'PULP_TRACE_PATH','PULP_TRACE_SECONDS','PULP_TRACE_RING_KB')
foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
$env:PULP_TEST_SIGNAL='noise'
$env:PULP_TEST_SIGNAL_AMPLITUDE='0.1'
$env:SPECTR_LFO_PERF_FIXTURE='1'
$env:SPECTR_BANDS_PERF_FIXTURE='1'
if($Trace){
 $env:PULP_TRACE_PATH="$out\ui.pftrace"
 $env:PULP_TRACE_SECONDS=[string]$TraceSeconds
 $env:PULP_TRACE_RING_KB='524288'
}else{
 $env:SPECTR_STATE_OUT="$out\live-state.json"
 $env:SPECTR_LAYOUT_DUMP="$out\live-layout.json"
 $env:SPECTR_MODULATION_DUMP="$out\modulation.json"
}
$started=[DateTime]::UtcNow
$info=New-Object Diagnostics.ProcessStartInfo
$info.FileName=$Exe
$info.WorkingDirectory=Split-Path -Parent $Exe
$info.UseShellExecute=$false
$info.RedirectStandardInput=$true
$info.RedirectStandardOutput=$true
$info.RedirectStandardError=$true
$info.CreateNoWindow=$true
$p=New-Object Diagnostics.Process
$p.StartInfo=$info
if(-not $p.Start()){throw 'Standalone did not start'}
$p.StandardInput.Close()
$stdout=$p.StandardOutput.ReadToEndAsync()
$stderr=$p.StandardError.ReadToEndAsync()
$identity=$p.StartTime
@{exe=$Exe;sha256=(Get-FileHash $Exe -Algorithm SHA256).Hash;pid=$p.Id;start=$identity.ToString('o');launched_utc=$started.ToString('o');trace=[bool]$Trace;scenario=$Scenario;trace_seconds=$TraceSeconds;session=$session;audio='platform default, noise amplitude 0.1';fixture='64 bands, LFO1'} | ConvertTo-Json | Set-Content "$out\identity.json"
$clock=[Diagnostics.Stopwatch]::StartNew()
$phases=[Collections.Generic.List[object]]::new()
function CheckOwned {
 $p.Refresh()
 if($p.HasExited -or $p.StartTime -ne $identity){throw 'Owned process exited or identity changed'}
 [uint32]$owner=0
 [StressWindow]::GetWindowThreadProcessId($script:child,[ref]$owner)|Out-Null
 if($owner -ne $p.Id){throw 'Editor HWND ownership changed'}
}
function Mouse([uint32]$msg,[double]$x,[double]$y,[int]$buttons){
 CheckOwned
 $r=New-Object StressWindow+Rect
 [StressWindow]::GetClientRect($script:child,[ref]$r)|Out-Null
 # Coordinates are normalized to the authored editor. Preserve aspect and
 # account for letterboxing rather than stretching input independently.
 $scale=[Math]::Min(($r.R-$r.L)/990.0,($r.B-$r.T)/645.0)
 $px=[int](($r.R-$r.L-990*$scale)/2+$x*990*$scale)
 $py=[int](($r.B-$r.T-645*$scale)/2+$y*645*$scale)
 $packed=($py -shl 16) -bor ($px -band 65535)
 $result=[IntPtr]::Zero
 if([StressWindow]::SendMessageTimeout($script:child,$msg,[IntPtr]$buttons,[IntPtr]$packed,2,5000,[ref]$result) -eq [IntPtr]::Zero){throw "Editor timed out on message $msg"}
}
function Click([double]$x,[double]$y){Mouse 0x201 $x $y 1;Mouse 0x202 $x $y 0}
function Escape {
 CheckOwned
 $result=[IntPtr]::Zero
 foreach($msg in @(0x100,0x101)){
  if([StressWindow]::SendMessageTimeout($script:child,$msg,[IntPtr]27,[IntPtr]0,2,5000,[ref]$result) -eq [IntPtr]::Zero){throw 'Editor timed out on Escape'}
 }
}
function Capture([string]$name){
 if($Trace){return}
 $r=New-Object StressWindow+Rect
 [StressWindow]::GetWindowRect($script:top,[ref]$r)|Out-Null
 [uint32]$owner=0
 [StressWindow]::GetWindowThreadProcessId([StressWindow]::GetForegroundWindow(),[ref]$owner)|Out-Null
 if($owner -ne $p.Id){throw 'Refusing an obscured screenshot'}
 $bmp=[Drawing.Bitmap]::new($r.R-$r.L,$r.B-$r.T)
 $g=[Drawing.Graphics]::FromImage($bmp)
 try{$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size);$bmp.Save("$out\$name.png")}finally{$g.Dispose();$bmp.Dispose()}
 foreach($item in @('live-state.json','live-layout.json','modulation.json')){
  if(Test-Path "$out\$item"){Copy-Item -LiteralPath "$out\$item" -Destination "$out\$name-$item"}
 }
}
try{
 for($i=0;$i -lt 40;$i++){Start-Sleep -Milliseconds 500;$p.Refresh();if($p.HasExited){throw "Exited $($p.ExitCode)"};if($p.MainWindowHandle -ne 0){break}}
 $script:top=$p.MainWindowHandle
 if($script:top -eq 0){throw 'No visible standalone window'}
 $script:child=[IntPtr]::Zero
 $find={param([IntPtr]$h,[IntPtr]$unused)
  $name=[Text.StringBuilder]::new(256)
  [StressWindow]::GetClassName($h,$name,256)|Out-Null
  if($name.ToString() -match 'Pulp.*Plugin|PulpPlugin'){$script:child=$h;return $false}
  return $true
 }
 [StressWindow]::EnumChildWindows($script:top,[StressWindow+EnumProc]$find,[IntPtr]::Zero)|Out-Null
 if($script:child -eq [IntPtr]::Zero){throw 'No SDK editor HWND; refusing guessed input target'}
 [StressWindow]::SetForegroundWindow($script:top)|Out-Null
 Start-Sleep -Seconds 4
 $workload=switch($Scenario){
  'gesture'{@('idle','draw')}
  'settings'{@('idle','settings','settings-close')}
  'presets'{@('idle','presets','manager','manager-close')}
  'resize'{@('idle','maximized','restored')}
  default{@('idle','draw','settings','settings-close','presets','manager','manager-close','maximized','restored')}
 }
 foreach($phase in $workload){
  CheckOwned
  $begin=$clock.Elapsed.TotalMilliseconds
  switch($phase){
   'draw'{
    Mouse 0x201 0.15 0.50 1
    for($i=1;$i -le 180;$i++){
     Mouse 0x200 (0.15+0.65*$i/180) (0.50-0.12*[Math]::Sin($i/18)) 1
     Start-Sleep -Milliseconds 20
    }
    Mouse 0x202 0.80 0.50 0
   }
   'settings'{Click (935.0/990) (624.0/645)}
   'settings-close'{Escape}
   'presets'{Click (315.0/990) (624.0/645)}
   'manager'{Click (315.0/990) (598.0/645)}
   'manager-close'{Escape}
   'maximized'{[StressWindow]::ShowWindow($script:top,3)|Out-Null}
   'restored'{[StressWindow]::ShowWindow($script:top,9)|Out-Null}
  }
  Start-Sleep -Seconds 3
  $phases.Add(@{name=$phase;begin_ms=$begin;end_ms=$clock.Elapsed.TotalMilliseconds;end_utc=[DateTime]::UtcNow.ToString('o')})
  Capture $phase
 }
 if($Trace){
  $deadline=$started.AddSeconds($TraceSeconds+30)
  while(-not (Test-Path "$out\ui.pftrace")){
   if([DateTime]::UtcNow -gt $deadline){throw 'Trace did not flush by its deadline'}
   CheckOwned
   Start-Sleep -Milliseconds 500
  }
  if((Get-Item "$out\ui.pftrace").Length -le 4096){throw 'Trace is too small; runtime tracing remains unproven'}
 }
 'CAPTURED_REQUIRES_STATE_VISUAL_AUDIO_AND_TRACE_REVIEW' | Set-Content "$out\status.txt"
}catch{
 $_ | Out-String | Set-Content "$out\error.txt"
 throw
}finally{
 $phases | ConvertTo-Json -Depth 5 | Set-Content "$out\phases.json"
 $p.Refresh()
 if(-not $p.HasExited -and $p.StartTime -eq $identity){
  $p.CloseMainWindow()|Out-Null
  $exited=$p.WaitForExit(15000)
 }else{$exited=$true}
 if($exited){
  $p.WaitForExit()
  [IO.File]::WriteAllText("$out\stdout.txt",$stdout.Result)
  [IO.File]::WriteAllText("$out\stderr.txt",$stderr.Result)
 }
 @{clean_exit=$exited;exit_code=$(if($exited){$p.ExitCode}else{$null})} | ConvertTo-Json | Set-Content "$out\exit.json"
}
