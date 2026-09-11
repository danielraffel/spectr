# What the native editor actually executes

`materialized-document.runtime.json` is the shipping editor. The native host
loads it; it is what every native test drives and what every measurement in
this repository is measuring.

## `resources/editor.html` is not in this graph

The two files have diverged. Reasoning about shipping behavior from
`editor.html` reads code the product never runs, and it has already produced
one incorrect finding. The divergence is measurable rather than asserted — a
keyword census with working controls gives `ew-resize` **0** here against **2**
in `editor.html`, while `col-resize` (6 / 7) and `grab` (8 / 12) are non-zero
in both, so the instrument is live and the zero is real.

If you need to know what a control does on screen, read this file.

## Cursor state has two writers in the same branch

The hover branch sets the cursor twice: through `setCursor(...)` and again
imperatively through `wrapRef.current.style.cursor = ...`. Both supply the same
value, so **breaking one leaves the other intact** and the assertion still
passes. That reads as a non-discriminating test when the test is fine.

Any negative control over cursor behavior must break **both** writers. A
single-site break is not a control, and a report built on one is not evidence.

## This file is generated output

Hand-editing it is a repair, not a fix: the change belongs in the source layer
and must be transplanted. It is minified onto one logical line with
inconsistent `/` escaping, so a `json.loads` / `json.dumps` round-trip cannot
reproduce it byte-for-byte — any tool that reformats before writing will
produce a diff that is entirely noise.
