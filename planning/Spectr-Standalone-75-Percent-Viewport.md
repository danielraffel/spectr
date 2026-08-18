# Standalone renders its editor into ~75% of the window

**Status:** OPEN, unresolved. Reproducible observation with a strong, untested hypothesis.
**Found:** 2026-08-17, during the top/bottom bar alignment investigation.
**Branch at time of observation:** `feature/native-state-parity-20260814` @ `738963a` (since reverted by `52d2aa0`).
**Why this file exists:** the symptom is independent of every premise that shifted during that
session. If the move to proportional-only resizing makes it disappear, close this. If it survives
a pinned viewport, it was never the responsive layer and whoever picks it up should start here
rather than re-deriving it.

---

## The observation

The Spectr standalone draws its editor into roughly the top-left 75% of its window, with the
remainder painted black. It is not a crop and not a clipped layout: the entire editor is present
and correctly laid out, just smaller than the window it lives in.

Reproduced twice, minutes apart, from separate process launches:

| | launch 1 | launch 2 |
|---|---|---|
| PID | 58339 | 75207 |
| `winrect "Spectr"` | `100 205 990 677` | `165 233 892 610` |
| capture size | 1980 x 1354 | 1980 x 1354 |
| title bar | 64 px | 64 px |
| ink bounds (thresh > 25) | x 0..1979, y 0..1010 | x 0..1979, y 0..1010 |

Launch 2 included a 6-second settle after the editor-open log line and a raise-click on the title
bar, to rule out capturing a mid-composite frame. The ink bounds came out **identical**, so this is
not a capture-timing artifact.

Host log, identical in both launches:

```
[gpu-host] first frame: logical=990x645 gpu=1980x1290 scale=2.0
Standalone: editor window open (990x645, gpu=true, mode=scripted, chrome=editor-only, inspector=ready)
```

The host believes it is 990x645 at 2x. It is not confused about its own size.

## The measurement that pins the scale factor

Profiling the top rail's ink columns in the capture and reading them as design pixels:

- interpreted at `designw = 1320` (i.e. 1.5 capture px per design px), the runs land at
  brand `x 21.3..92.0`, LIVE pill `370.0..394.7`, trailing `RES` run ending `977.3`
- the headless render of the **same layout at 990** puts them at
  brand `21.0..92.0`, LIVE `370.0..395.0`, `RES` ending `989.5`

Those match. So the content is the **responsive-990 layout**, drawn at **1.5 capture px per design
px** where the surface is 2.0 capture px per point. That is a uniform **0.75** scale, and it puts
990 design px into 742.5 pt of a 990 pt window — the observed ~75%. The bottom rail's rightmost
ink independently lands at x = 1485 of 1980, which is 0.75 exactly.

Note the content is the *reflowed* layout, not the authored one — the LIVE pill is at design
x = 370 (responsive) rather than x = 700 (authored). Both mechanisms are visibly active at once.

## Leading hypothesis

**The standalone pins a 1320-wide design viewport while the responsive layer independently reflows
to 990, and the two compose into a 0.75 shrink.**

`0.75 = 990 / 1320` exactly, and 1320 is Spectr's authored design width. The arithmetic works with
the chrome allowance too: a pinned `1320 x (860 + chrome)` viewport in a `990 x 677` window fits at
`min(990/1320, 677/(860+chrome))` = `min(0.750, 0.759)` = **0.750** for a chrome height of ~32 px.
The hypothesis reproduces the measured factor to three digits.

The suspect path, in the SDK the standalone actually builds against
(`/tmp/pulp-sdk-spectr-current`, per `build-native-current-release/CMakeCache.txt`):

```
core/format/src/standalone.cpp
  -> detail::configure_standalone_design_viewport(*window, size_hints, chrome)
       include/pulp/format/detail/standalone_editor_chrome.hpp
         if (should_pin_design_viewport(size_hints))
             window.set_design_viewport(standalone_design_viewport_size(...))
         if (should_lock_view_aspect(size_hints))
             window.set_fixed_aspect_ratio(size_hints.aspect_ratio)

  standalone_design_viewport_size() returns design_viewport_width(size_hints),
  and include/pulp/format/plugin_descriptor.hpp:54 defines that as
      hints.design_width > 0 ? hints.design_width : hints.preferred_width
```

Spectr sets `design_width/height = 1320x860` in `make_editor_view_size()`
(`include/spectr/editor_resize.hpp`), so **that helper returns 1320, not 990.**

The thing that is *supposed* to prevent the pin is the guard:

```
should_pin_design_viewport(hints):
    if (hints.viewport_policy == ViewportPolicy::Responsive) return false;
```

and Spectr does set `viewport_policy = Responsive` in the same function. So on a straight reading
the viewport should never be pinned and there should be no 0.75. **Something makes it happen
anyway, and that gap is the open question.**

## What was ruled out

- **Capture timing / mid-composite frame.** Two launches, one with a 6 s settle and a raise-click,
  produced byte-identical ink bounds.
- **The host being confused about its size.** It logs `logical=990x645 gpu=1980x1290 scale=2.0`.
- **A clipped or overflowing layout.** The whole editor is present and internally correct; it is
  uniformly smaller.
- **Window-frame variance being the cause.** `winrect` reported different frames on the two
  launches (990x677 vs 892x610) yet the captured surface and ink bounds were identical, so the
  75% does not track the on-screen frame.

## What was NOT ruled out, and the trap that produced a wrong answer once

- **Whether this predates `738963a`.** Every live launch was made with that commit in the tree.
  The commit touches no transform and only edits layout arithmetic inside
  `applySpectrResponsiveLayout`, so it is very unlikely to be the cause — but it was never tested
  against a clean tree live. **Now cheap to settle**, since `52d2aa0` reverted it: build the
  standalone from current HEAD, launch, and re-measure the ink bounds.
- **Whether the AU / plugin host shows the same shrink.** Logic was never launched during this
  investigation.
- **Why `should_pin_design_viewport` does not prevent it.** Candidates worth checking, in order:
  whether the built SDK's header actually matches the one read here, whether the
  `if constexpr (requires ...)` probe in `make_editor_view_size` genuinely finds
  `viewport_policy` on that SDK's `ViewSize` (if the probe misses, the field keeps its default
  `ViewportPolicy::Automatic`, and **Automatic pins whenever the editor is resizable with a
  non-zero aspect ratio — which is exactly Spectr**), and whether some path other than
  `configure_standalone_design_viewport` applies a design viewport.

**The `if constexpr` probe is the single most promising lead.** If it silently fails, Spectr thinks
it asked for `Responsive` while the SDK sees `Automatic` and pins the 1320 viewport — which
produces precisely this symptom, with no error anywhere.

> **Source-tree trap — this cost a wrong retraction during the session.** There are two copies of
> `standalone_editor_chrome.hpp` on this machine and **they differ in exactly the code above**:
> `/Users/danielraffel/Code/pulp/...` returns `size_hints.preferred_width` and has **no**
> `should_pin_design_viewport` guard, while
> `/tmp/pulp-sdk-spectr-current/...` returns `design_viewport_width(...)` and **does** guard.
> Spectr builds against the second. Reading the first led to declaring the 1320-viewport theory
> disproven when it is in fact the leading hypothesis. **Always read the SDK path from the build
> directory's `CMakeCache.txt` (`Pulp_DIR`), with an absolute path, not whatever tree the shell
> happens to be sitting in.**

## How to reproduce

```sh
cd /Users/danielraffel/Code/spectr-native-state-parity-20260814
cmake --build build-native-current-release --target Spectr_Standalone -j6
./build-native-current-release/Spectr.app/Contents/MacOS/Spectr &   # opens an audio device
# wait for "editor window open" in the log, then settle ~6s
caffeinate -u -t 1
screencapture -x -o -l"$(/tmp/winid Spectr | head -1 | tr -dc '0-9')" /tmp/live.png
```

Then measure the ink bounds; content filling the surface means fixed, ink stopping near
x = 1485 of 1980 means still present:

```py
from PIL import Image
im = Image.open('/tmp/live.png').convert('L'); px = im.load(); W, H = im.size
xs = [x for x in range(W) if any(px[x, y] > 25 for y in range(0, H, 3))]
ys = [y for y in range(H) if any(px[x, y] > 25 for x in range(0, W, 3))]
print(im.size, xs[0], xs[-1], ys[0], ys[-1])
```

Terminate by PID when done — the standalone holds an audio device open.

## Bearing on the proportional-only decision

This cuts both ways and is worth understanding before the switch lands.

If the hypothesis is right, moving Spectr to `ViewportPolicy::FixedDesign` makes
`should_pin_design_viewport` return **true deliberately**, and the 1320x860 viewport gets pinned on
purpose — which is the intended proportional-only behaviour. The symptom then disappears **only if
`applySpectrResponsiveLayout` stops reflowing at the same time.** Pinning the viewport while
leaving the responsive reflow in place would preserve this bug rather than fix it, because the
reflow is the other half of the double-application.

So: the two changes are a pair, not independent. Land them together, and re-run the reproduction
above afterwards to confirm the window fills.


## RESOLVED 2026-08-18 — the JS reflow was the mechanism, not a viewport pin

Closed by spectr `4e43917` (pin the editor viewport so resize is proportional only).

**Acceptance measurement**, standalone rebuilt with the package, launched and
captured:

```
window 1320x892 (authored 1320x860 + title bar)
[gpu-host] first frame: logical=1320x860 gpu=2640x1720 scale=2.0
rightmost ink 2614/2640 = 99.0%      lowest ink 1744/1784 = 97.8%
```

Against the 75% recorded above (ink ending at 1485 of 1980). The remainder is
rounded corners and title bar. Backing scale is a clean **2.0**, not the 1.5
measured before — no 0.75 factor anywhere in the pipeline.

**Which hypothesis won.** The pin theory in this document is NOT what was
happening. Two independent checks say the pin was never silently on:

- All three SDKs named by `Pulp_DIR` declare `viewport_policy`, so the
  `if constexpr (requires(...))` probe HITS and `Responsive` really was set.
- `should_pin_design_viewport()` returns false on its first line for
  `Responsive`, unconditionally.

The actual mechanism needs no pin at all: `applySpectrResponsiveLayout` calls
`applyMaterializedImportMetadata`, which restores every node to its authored
1320x860 geometry before re-placing it. Any path that lays content out in
authored units and presents it in a 990 window yields 0.75 — and 990 IS 0.75 of
1320 by construction, since `SPECTR_HOST_PREFERRED` is defined as 0.75 of
authored. Removing the reflow removed the symptom.

**The pruning hazard was re-tested under the pin**, since the earlier disproof
was for `Responsive` and did not transfer: the rig asserts the root stays at the
authored box while the host varies, and the tap-target sweep passes 3659
assertions across 792x516 / 990x645 / 1320x860 / 2640x1720 with every control
hit-testable.

**Method note worth keeping.** The confident disproof recorded above was made
against `~/Code/pulp`, but the standalone builds against the SDK named by
`Pulp_DIR` in its `CMakeCache.txt`. The two copies of
`standalone_editor_chrome.hpp` differ in exactly the code that was being read.
Always resolve `Pulp_DIR` and read the SDK copy; there are several staged SDKs
on this machine and different Spectr build dirs point at different ones.
