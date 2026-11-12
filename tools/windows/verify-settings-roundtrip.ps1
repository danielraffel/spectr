<# Verify actual state transitions after a functional settings workload.
   Diagnostic readbacks are deliberately excluded from timed profiling. #>
[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$EvidenceDir)
$ErrorActionPreference='Stop'
$out=(Resolve-Path -LiteralPath $EvidenceDir).Path
$resultPath=Join-Path $out 'settings-roundtrip-result.json'
if(Test-Path -LiteralPath $resultPath){throw 'Result already exists; preserve it and use fresh evidence'}
function ReadJson([string]$name){
 [IO.File]::ReadAllText((Join-Path $out $name)) | ConvertFrom-Json
}
$identity=ReadJson 'identity.json'
$exit=ReadJson 'exit.json'
if($identity.trace){throw 'Diagnostic settings readbacks cannot qualify a timed trace'}
if(!$exit.clean_exit -or $exit.exit_code -ne 0){throw 'Settings roundtrip requires clean exit'}
if($identity.sha256 -notmatch '^[A-Fa-f0-9]{64}$'){throw 'Missing executable hash'}
$rows=@(foreach($line in [IO.File]::ReadAllLines((Join-Path $out 'stderr.txt'))){
 $marker='[settings-readback] '
 $index=$line.IndexOf($marker,[StringComparison]::Ordinal)
 if($index -ge 0){$line.Substring($index+$marker.Length) | ConvertFrom-Json}
})
$expected=[ordered]@{
 'settings'=@('spectral','columns','both')
 'settings-mono'=@('mono','columns','both')
 'settings-liquid'=@('mono','liquid','both')
 'settings-bars'=@('mono','liquid','bars')
 'settings-spectral'=@('spectral','liquid','bars')
 'settings-columns'=@('spectral','columns','bars')
 'settings-both'=@('spectral','columns','both')
}
# Windows PowerShell 5.1 emits the parsed root array as a single pipeline
# object. Wrapping the function call in @() would introduce a nested array.
$phases=ReadJson 'phases.json'
$checks=@(foreach($name in $expected.Keys){
 $phase=@($phases | Where-Object name -eq $name)
 if($phase.Count -ne 1){throw "Expected exactly one phase: $name"}
 $phase=$phase[0]
 # PowerShell 7 may deserialize ISO dates as DateTime; 5.1 keeps strings.
 # Parsing a DateTime's localized string would discard its UTC kind.
 $endTime=if($phase.end_utc -is [DateTime]){[DateTimeOffset]$phase.end_utc}else{
  [DateTimeOffset]::Parse($phase.end_utc,[Globalization.CultureInfo]::InvariantCulture)
 }
 $end=$endTime.ToUnixTimeMilliseconds()
 $duration=[double]$phase.end_ms-[double]$phase.begin_ms
 if($duration -le 0){throw "Invalid phase duration: $name"}
 $samples=@($rows | Where-Object {$_.t -ge ($end-$duration/2) -and $_.t -le $end})
 if(!$samples.Count){throw "No late-phase state readback: $name"}
 $last=$samples[-1]
 $actual=@($last.settings.theme,$last.settings.metaphor,$last.visualizationMode)
 if(($actual -join '/') -cne ($expected[$name] -join '/')){throw "Settings state mismatch: $name got $($actual -join '/')"}
 @{phase=$name;expected=$expected[$name];actual=$actual;late_samples=$samples.Count;pass=$true}
})
$sizes=@{}
foreach($name in @('maximized','restored')){
 $geometry=ReadJson ($name+'-hwnd.json')
 if($geometry.child.width -le 0 -or $geometry.child.height -le 0 -or
    $geometry.top.width -ne $geometry.child.width -or
    $geometry.top.height -ne $geometry.child.height){throw "Parent/child sizing mismatch: $name"}
 $sizes[$name]=$geometry
}
@{scope='standalone functional settings and native HWND sizing; inspect screenshots for clipping';
 exe_sha256=$identity.sha256;session=$identity.session;checks=$checks;native_geometry=$sizes;
 clean_exit=$true;pass=$true;limitations=@('Does not prove full-screen fill, visual parity, restart persistence, installed plugins or performance')
} | ConvertTo-Json -Depth 8 | Set-Content $resultPath
