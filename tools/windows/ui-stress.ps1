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
 [ValidateSet('full','gesture','settings','presets','resize','preset-roundtrip')][string]$Scenario='full',
 [ValidateRange(15,120)][int]$TraceSeconds=65,
 [ValidateRange(12,720)][int]$DragSamples=180,
 [ValidateRange(3,30)][int]$SettleSeconds=3,
 [ValidateSet('default','cpu','gpu-timed')][string]$RenderProbe='default',
 [switch]$DisableStaticRasterCache,
 [string]$PaintCostDump,
 [string]$DiagnosticScript
)
$ErrorActionPreference='Stop'
if($Trace -and $PaintCostDump){throw 'Paint-cost file I/O is forbidden during a timed trace'}
if($Trace -and $Scenario -eq 'preset-roundtrip'){throw 'Preset roundtrip requires functional snapshots; run it separately from timing'}
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
 'PULP_TRACE_PATH','PULP_TRACE_SECONDS','PULP_TRACE_RING_KB','PULP_LOG_CANVAS_PAINT')
# These are process-local test controls. An old diagnostic must never silently
# remove decoration, force a backend or write per-frame files in another run.
$names+=@(Get-ChildItem Env: | Where-Object Name -Like 'SPECTR_*' | ForEach-Object Name)
foreach($name in $names){[Environment]::SetEnvironmentVariable($name,$null,'Process')}
if($DisableStaticRasterCache){$env:SPECTR_DISABLE_STATIC_RASTER_CACHE='1'}
if($RenderProbe -ne 'default'){$env:SPECTR_WINDOWS_RENDER_PROBE=$RenderProbe}
if($PaintCostDump){$env:SPECTR_PAINT_COST_DUMP=[IO.Path]::GetFullPath($PaintCostDump)}
if($DiagnosticScript){$env:SPECTR_EVAL=$DiagnosticScript}
$env:PULP_TEST_SIGNAL='noise'
$env:PULP_TEST_SIGNAL_AMPLITUDE='0.1'
$env:SPECTR_LFO_PERF_FIXTURE='1'
$env:SPECTR_BANDS_PERF_FIXTURE='1'
if($Trace){
 $env:PULP_TRACE_PATH="$out\ui.pftrace"
 # Close the owned app to stop tracing between frames. A timer can cut an
 # acquire/frame in half, leaving unfinished spans in an otherwise valid file.
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
@{exe=$Exe;sha256=(Get-FileHash $Exe -Algorithm SHA256).Hash;pid=$p.Id;start=$identity.ToString('o');launched_utc=$started.ToString('o');trace=[bool]$Trace;scenario=$Scenario;trace_seconds=$TraceSeconds;session=$session;audio='platform default, noise amplitude 0.1';fixture='64 bands, LFO1';renderer_probe=$RenderProbe;static_raster_cache_disabled=[bool]$DisableStaticRasterCache;paint_cost_dump=$PaintCostDump;diagnostic_script=$DiagnosticScript;diagnostic_only=[bool]$DiagnosticScript} | ConvertTo-Json | Set-Content "$out\identity.json"
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
# Target the previous completed snapshot, not a file the editor is rewriting.
# Build non-ASCII menu labels from codepoints for Windows PowerShell 5.1.
function ClickText([string]$text){
 $layout=[IO.File]::ReadAllText($script:latestLayoutPath) | ConvertFrom-Json
 $targets=@($layout.nodes | Where-Object {
  $_.visible -and $_.rect.w -gt 0 -and $_.rect.h -gt 0 -and
  @($_.measured_text_boxes | Where-Object text -eq $text).Count -gt 0
 })
 if($targets.Count -ne 1){throw ("Expected one visible target: "+$text+" count="+$targets.Count)}
 $r=$targets[0].rect
 $px=($r.x+$r.w/2)/1320.0
 $py=($r.y+$r.h/2)/860.0
 Mouse 0x200 $px $py 0
 Click $px $py
}
function Escape {
 CheckOwned
 $result=[IntPtr]::Zero
 foreach($msg in @(0x100,0x101)){
  if([StressWindow]::SendMessageTimeout($script:child,$msg,[IntPtr]27,[IntPtr]0,2,5000,[ref]$result) -eq [IntPtr]::Zero){throw 'Editor timed out on Escape'}
 }
}
function Capture([string]$name){
 if($Trace){return}
 $rects=@{}
 foreach($part in @('top','child')){
  $handle=if($part -eq 'top'){$script:top}else{$script:child}
  $rect=New-Object StressWindow+Rect
  [StressWindow]::GetClientRect($handle,[ref]$rect)|Out-Null
  $rects[$part]=@{width=$rect.R-$rect.L;height=$rect.B-$rect.T}
 }
 $rects|ConvertTo-Json|Set-Content "$out\$name-hwnd.json"
 $r=New-Object StressWindow+Rect
 [StressWindow]::GetWindowRect($script:top,[ref]$r)|Out-Null
 [uint32]$owner=0
 [StressWindow]::GetWindowThreadProcessId([StressWindow]::GetForegroundWindow(),[ref]$owner)|Out-Null
 if($owner -ne $p.Id){throw 'Refusing an obscured screenshot'}
 $bmp=[Drawing.Bitmap]::new($r.R-$r.L,$r.B-$r.T)
 $g=[Drawing.Graphics]::FromImage($bmp)
 try{$g.CopyFromScreen($r.L,$r.T,0,0,$bmp.Size);$bmp.Save("$out\$name.png")}finally{$g.Dispose();$bmp.Dispose()}
 foreach($item in @('live-state.json','live-layout.json','modulation.json')){
  if(Test-Path "$out\$item"){
   # The application rewrites these live files. Copying during truncation can
   # preserve an empty or partial JSON document as an apparent snapshot.
   $valid=$false
   for($attempt=0;$attempt -lt 30;$attempt++){
    try{
     $json=[IO.File]::ReadAllText("$out\$item")
     if([string]::IsNullOrWhiteSpace($json)){throw 'Empty live JSON'}
     $null=ConvertFrom-Json -InputObject $json -ErrorAction Stop
     [IO.File]::WriteAllText("$out\$name-$item",$json)
     $valid=$true;break
    }catch{Start-Sleep -Milliseconds 100}
   }
   if(!$valid){throw "Could not capture complete JSON: $item"}
  }
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
  'preset-roundtrip'{@('idle','draw','presets','manager','save-open','save-submit','manager-close','clear-saved','presets-reopen','manager-reopen','select-saved','load-saved','loaded')}
  default{@('idle','draw','settings','settings-close','presets','manager','manager-close','maximized','restored')}
 }
 foreach($phase in $workload){
  CheckOwned
  $begin=$clock.Elapsed.TotalMilliseconds
  switch($phase){
   'draw'{
    Mouse 0x201 0.15 0.50 1
    for($i=1;$i -le $DragSamples;$i++){
     Mouse 0x200 (0.15+0.65*$i/$DragSamples) (0.50-0.12*[Math]::Sin(10.0*$i/$DragSamples)) 1
     Start-Sleep -Milliseconds 20
    }
    Mouse 0x202 0.80 0.50 0
   }
   'settings'{Click (935.0/990) (624.0/645)}
   'settings-close'{Escape}
   'presets'{if($Scenario -eq 'preset-roundtrip'){ClickText ('PRESET'+[char]0x2026+' '+[char]0x25be)}else{Click (315.0/990) (624.0/645)}}
   'manager'{if($Scenario -eq 'preset-roundtrip'){ClickText ('MANAGE'+[char]0x2026)}else{Click (315.0/990) (598.0/645)}}
   'save-open'{ClickText 'SAVE CURRENT'}
   'save-submit'{ClickText 'SAVE'}
   'clear-saved'{ClickText 'CLEAR'}
   'presets-reopen'{ClickText ('PRESET'+[char]0x2026+' '+[char]0x25be)}
   'manager-reopen'{ClickText ('MANAGE'+[char]0x2026)}
   'select-saved'{ClickText 'PATTERN 01'}
   'load-saved'{ClickText 'APPLY'}
   'manager-close'{Escape}
   'maximized'{[StressWindow]::ShowWindow($script:top,3)|Out-Null}
   'restored'{[StressWindow]::ShowWindow($script:top,9)|Out-Null}
  }
  Start-Sleep -Seconds $SettleSeconds
  $phases.Add(@{name=$phase;begin_ms=$begin;end_ms=$clock.Elapsed.TotalMilliseconds;end_utc=[DateTime]::UtcNow.ToString('o')})
  Capture $phase
  if(!$Trace){$script:latestLayoutPath="$out/$phase-live-layout.json"}
 }
 if($Trace){
  while($clock.Elapsed.TotalSeconds -lt $TraceSeconds){
   CheckOwned
   Start-Sleep -Milliseconds 500
  }
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
 if($exited -and $stderr.Result -notmatch 'mode=scripted'){
  'FAILED_SCRIPTED_EDITOR_REQUIRED' | Set-Content "$out\status.txt"
  throw 'Spectr did not open its scripted editor; fallback UI cannot pass this workload'
 }
 if($Trace -and $exited -and (-not (Test-Path "$out\ui.pftrace") -or (Get-Item "$out\ui.pftrace").Length -le 4096)){
  throw 'Trace did not flush on clean shutdown; runtime tracing remains unproven'
 }
}

if($Scenario -eq 'preset-roundtrip'){
 $exit=[IO.File]::ReadAllText("$out/exit.json") | ConvertFrom-Json
 if(!$exit.clean_exit -or $exit.exit_code -ne 0){throw 'Preset roundtrip requires clean exit'}
 $saved=[IO.File]::ReadAllText("$out/draw-live-state.json") | ConvertFrom-Json
 $cleared=[IO.File]::ReadAllText("$out/clear-saved-live-state.json") | ConvertFrom-Json
 $loaded=[IO.File]::ReadAllText("$out/loaded-live-state.json") | ConvertFrom-Json
 foreach($state in @($saved,$cleared,$loaded)){
  if($state.n_visible -ne 64 -or $state.gain_db.Count -ne 64){throw 'Unexpected band count'}
  foreach($value in $state.gain_db){
   if([double]::IsNaN($value) -or [double]::IsInfinity($value)){throw 'Nonfinite band value'}
  }
 }
 if(@($saved.gain_db | Where-Object {[Math]::Abs($_) -gt 0.1}).Count -lt 10){throw 'Saved state lacks drawn pattern'}
 if(@($cleared.gain_db | Where-Object {[Math]::Abs($_) -gt 0.0001}).Count){throw 'Clear did not reset bands'}
 $maxDelta=0.0
 for($i=0;$i -lt 64;$i++){$maxDelta=[Math]::Max($maxDelta,[Math]::Abs($saved.gain_db[$i]-$loaded.gain_db[$i]))}
 if($maxDelta -gt 0.0001){throw ('Reloaded band values differ by '+$maxDelta)}
 @{scope='in-session save/clear/reload via selection and Apply';bands=64;maximum_gain_delta=$maxDelta;exe_sha256=(Get-FileHash $Exe -Algorithm SHA256).Hash;clean_exit=$exit.clean_exit} | ConvertTo-Json | Set-Content "$out/roundtrip-result.json"
}
