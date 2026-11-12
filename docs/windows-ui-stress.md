# Windows UI stress checks

Run `tools/windows/ui-stress.ps1` in the logged-in Windows desktop session.
The driver owns its standalone process and checks editor HWND ownership. Use a
fresh evidence directory for each run. Preserve user DAW sessions.

For a functional preset check with live test audio and 64-band modulation:

```powershell
./tools/windows/ui-stress.ps1 -Exe C:/builds/Spectr.exe `
  -EvidenceDir C:/artifacts/preset-roundtrip-unique `
  -Scenario preset-roundtrip -SettleSeconds 12 -DragSamples 36
```

The check draws a pattern, saves the default `PATTERN 01`, closes the manager,
clears the bands, reopens the manager, selects the pattern and clicks Apply.
It requires at least ten changed bands before saving, all 64 cleared gains to
be zero, restored gains within 0.0001 dB, finite values and a clean exit.
`roundtrip-result.json` binds this in-session result to the executable hash.
Inspect the screenshots and live state alongside that receipt. This check
does not establish persistence across application restarts or installed
VST3/CLAP acceptance.

Layout targeting reads the previous completed snapshot. Reading the live file
directly can race the editor's writer. Targets must have a unique visible
text match and nonzero bounds. Menu punctuation uses Unicode codepoints so
Windows PowerShell 5.1 can read the driver without an encoding-dependent label.

Keep this functional scenario separate from `-Trace`: snapshots and screenshots
must not contaminate timed profiling. Use an existing trace-compatible scenario
for Perfetto, with no builds or screenshot captures during timing.

The preset check uses selection plus Apply. Simulated double-clicking failed
to apply a saved pattern in a slow ARM64EC VM capture; double-click behavior
remains a separate qualification item.

For settings changes and maximize/restore checks, use `-Scenario
settings-roundtrip -SettleSeconds 12 -DragSamples 36`. This draws bands,
opens settings and clicks Mono, Liquid, Bars, Spectral, Columns and Both.
It then closes settings, maximizes and restores the window. A diagnostic
readback records application state every second. The verifier requires the
expected final state in the latter half of each settings phase, a clean exit
and matching native parent/child client sizes. It rejects missing phases,
missing readbacks and stale result files.

`settings-roundtrip-result.json` records functional state and geometry only.
Inspect the screenshots separately for clipping and letterboxing. The scenario
cannot run with `-Trace` or a custom `-DiagnosticScript`; its logging affects
performance. Existing captures can be checked with
`tools/windows/verify-settings-roundtrip.ps1 -EvidenceDir <capture>` once.
Use a fresh capture directory to repeat the check while preserving old evidence.
