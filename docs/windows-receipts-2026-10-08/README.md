# Windows ARM64 desktop receipt — 2026-10-08

These receipts came from the disposable native ARM64 QEMU overlay on the M5S
host. The guest had an authenticated `admin` desktop session, the verified
Spectr package was installed, and REAPER ARM64 was launched.

The receipts prove:

- exact REAPER and Spectr VST3 paths and SHA-256 hashes;
- a real Windows desktop session was detected;
- REAPER launched successfully;
- REAPER recorded the Spectr VST3 in its scan cache.

They do **not** prove plugin instantiation, runtime audio, or Ableton support.
Those require an observed DAW track/FX load and an audio/render receipt. The
desktop session used a temporary password on the disposable overlay; it was
not written to the golden image.
