# Windows ARM64EC build receipt — 2026-10-09

This receipt records the first current ARM64EC Spectr plugin link on the native
Windows guest running under QEMU on the M5S. The Pulp follow-up is published at
https://github.com/Generous-Corp/pulp/pull/9973.

The VST3 and CLAP targets linked successfully after Pulp exported the generated
control-shipping marker with `__declspec(dllexport)`. `dumpbin /headers` reports
`8664 machine (x64) (ARM64X)`, which is the image type required by the ARM64EC
REAPER host boundary.

The aggregate Spectr test executable did not link because the installed SDK's
`pulp-audio-analysis.lib` is Debug-built while this Release test tree is not,
and the optional Claude bundle symbol is absent. That failure is recorded as a
separate SDK/test-harness consistency defect; it is not treated as plugin-test
success.

The plugin is still **not accepted in REAPER**. The required observed host
instance, failed-scan evidence, real render, desktop screenshot, Windows audio
harness/Quality Lab result, and Perfetto trace remain open. The existing helper
is fail-closed and must be run with a matching observed acceptance receipt; a
scan cache entry or `LoadLibrary` result is insufficient.
