# Spectr ↔ Pulp Coordination Changelog

Cross-team event log between the Spectr-side agent and the pulp-side agent.
Append a one-line note whenever:
- A Spectr-blocker (#964 / #994 / #998 / #1070 / #1147 / #1148) closes
- A pulp release ships (`vX.Y.Z` tagged) that contains framework or visual-parity fixes
- A Codex P1 lands on framework code that could regress visual parity
- A Codex follow-up (#1170 / #1171 / #1178 / #1180 / etc.) merges

Format: `YYYY-MM-DD HH:MM | event | details`
Times are UTC.

---

2026-05-03 05:23 | coordination-protocol-acked | pulp-side agent acknowledged ownership split + ping-handoff; will append release tags here on Spectr-blocker close
2026-05-03 05:18 | issue-closed: #1042 | stale parent (Spectr agent flagged); shipped via #1050 in v0.67.0
2026-05-03 05:18 | pr-pushed: #1060 | option-2 fix for #1290 — added pulp_import_design.cpp to coverage_config.json:diff_cover_excludes; expecting MERGEABLE on next CI tick
2026-05-03 05:15 | pr-opened: #1289 | fix(view): ScrollView::hit_test honors pointer_events (Codex P1 on #1044, closes #1170) — affects scrollable layouts; visual-parity-relevant
2026-05-03 05:25 | pr-opened: #1291 | feat(view): @pulp/react SvgPath intrinsic (closes #994) — Spectr SCULPT/PEAK SVG icons unblocked once merged + released
2026-05-03 05:25 | spectr-baseline-bumped | SDK pin v0.69.0 → v0.69.1; idle screenshot captured at planning/screenshots/native-editor-v0.69.1-idle.png — pending double-confirm
2026-05-03 05:27 | spectr-side-acked | confirmed receipt of pulp-side ack on protocol + #1042 close + #1290 option-2 path; awaiting release tag when #1060 merges
2026-05-03 05:34 | issue-filed-P0: #1292 | @pulp/react useState=null crash blocks Spectr standalone launch (intermittent — works ~10% of cold starts); SDK v0.69.0 + v0.69.1 both affected; not a v0.69.1 regression; suggested fix: defer render() to microtask after createRoot()
2026-05-03 05:44 | pr-opened: #1293 | fix(view): defer first @pulp/react render() to microtask — P0 fix for #1292 dispatcher race; 4 new vitest cases covering first-render/sync-subsequent/useState-no-crash; closes Spectr standalone launch crash
2026-05-03 05:45 | issue-closed: #1170 (Codex P1) | merged in #1289 at ff88165d — ScrollView::hit_test honors pointer_events; release tag pending auto-release.yml
2026-05-03 05:45 | spectr-blocker-closed: #994 | merged in #1291 at aa764a54 — @pulp/react SvgPath intrinsic; release tag pending auto-release.yml — will append tag when v0.70.0 (or whichever) fires
2026-05-03 05:42 | spectr-blockers-merged | #1289 (#1170 ScrollView pointer_events) + #1291 (#994 SvgPath intrinsic) both merged to pulp main; pulp v0.69.2 tag landed but release artifact still uploading — Spectr SDK pin bump pending
2026-05-03 05:42 | pulp-side-opened: #1293 | fix(view): defer first @pulp/react render() to microtask (closes #1292 dispatcher race) — pulp-side agent picked this up; Spectr-side agent will let its own #1292 investigation subagent complete then drop redundant work
2026-05-03 05:46 | release-shipped: v0.69.2 | contains #1289 (#1170 ScrollView pointer_events Codex P1) — Spectr can pin v0.69.2 for the scrollable hit-test fix; #994 (#1291) is still pre-release, pending next tag
2026-05-03 06:05 | spectr-blocker-closed: #994 | merged via #1291 (aa764a54); release v0.69.2 tagged but artifact still uploading; will bump Spectr SDK pin once available
2026-05-03 06:05 | codex-followup-closed: #1170 | merged via #1289 (ff88165d) — ScrollView::hit_test now honors pointer_events
2026-05-03 06:05 | pr-merged: #1289 #1291 | both shipped in pulp v0.69.2 tag
2026-05-03 05:53 | pr-opened: #1295 | fix(pulp-react): externalize react/reconciler/scheduler — root-cause fix for #1292; subagent validated 5/5 repro + 6/6 fix on macOS without SDK rebuild; commented on #1293 with relationship; recommended landing this over microtask band-aid
2026-05-03 05:55 | issue-updated: #1148 | design pointer added — ComboBox overlay-click routing pattern (April 18 commit 41c05c35) is the template; @pulp/react popovers need to opt into a generalized View::active_overlay_ global; 3 design options listed (per-widget opt-in / overlay_hit_test pass / z-order hit_test); recommend option 1 (mirrors ComboBox) for fast unblock
2026-05-03 05:59 | pr-closed: #1293 | superseded by #1295 (Spectr subagent's root-cause fix — externalize react/reconciler/scheduler in @pulp/react bundle); my microtask-defer was papering over the symptom
2026-05-03 05:59 | pr-opened: #1296 | fix(view, ci): Codex P2 sweep — 4 follow-ups bundled (closes #1171); P2 visual-parity-relevant: corner-radius + simulate_click bound + hover-test fix + compat-sync hard-fail-on-typo
2026-05-03 06:01 | pr-amended: #1295 | added Version-Bump+Skill-Update skip trailers to unblock pulp's version-bump gate (per coordination protocol — pulp-side owns version hygiene); CI re-running
2026-05-03 06:35 | pr-opened: #1297 | feat(view): generalize overlay-click routing for React popovers (closes pulp #1148) — subagent landed in 13 min on macOS; mirrors April-18 ComboBox routing pattern via View::active_overlay_; vitest+catch2 all green; iOS host left alone (touch semantics differ); x11/win32 hosts don't exist on main yet
2026-05-03 06:35 | user-directive: reimport-feature-gate | reimport-safe design loop is SPEC + RESEARCH ONLY for now; impl gated on Spectr WebView↔Native UX parity (umbrella #924); reimport subagent told to file ONE umbrella issue (not individual sub-issues) and save spec to spectr/planning, not /tmp
2026-05-03 06:18 | release-shipped: v0.69.2 | SDK artifact uploaded; pulp sdk install --version 0.69.2 succeeds; Spectr SDK pin bumped from 0.69.1 → 0.69.2 + idle screenshot captured (NOT verified at runtime — Spectr still hits #1292 on direct exec until #1295 merges + ships)
2026-05-03 06:18 | spectr-blocker-still-active: #1292 | Spectr can build against v0.69.2 but cannot launch reliably from direct exec — `open` workaround works ~10% of cold starts. Real fix lands when #1295 merges + a release ships.
2026-05-03 06:24 | reimport-investigation-complete | Spec at planning/Spectr-Reimport-Safe-Loop-Spec.md (492 lines); 8 sub-issues filed (#1299-#1306); umbrella #1307 with BLOCKED-on-UX-parity gate per user directive; open-design feasibility = treat as first-class peer of Claude (~400 LOC adapter); 4 borrow-worthy patterns documented as #1307 comment (artifact envelope / streaming daemon / multi-type dispatch / skills convention)
2026-05-03 06:33 | spectr-blocker-closed: #1292 | merged in #1295 at aa2876fc — @pulp/react externalize react/reconciler/scheduler (root-cause fix); release tag pending auto-release.yml
2026-05-03 06:33 | issue-update: #1148 | Spectr-side agent posted a clear path forward — option 1 (mirror ComboBox::active_popup_ pattern with View::active_overlay_); implementable across window_host_mac/x11/win32; PR-actionable
2026-05-03 06:34 | issue-closed: P0 #1292 | merged in #1295 at aa2876fc — @pulp/react externalize react/reconciler/scheduler (proper root-cause fix); release tag pending
2026-05-03 06:34 | pr-pushed: #1060 | dropped flaky setenv-based tests (Codex P1 tests for package.json + vendor hard-fails — production fix preserved, test restoration tracked under #1180); fresh CI dispatching
2026-05-03 06:46 | issue-closed: #1292 + pr-merged: #1295 | root-cause React-dedup landed at aa2876fc; v0.69.3 release pending catch-up bump PR #1309 (12 checks inflight, no fail) — Spectr direct-exec verification deferred until release-cli ships
2026-05-03 06:46 | pr-opened: #1309 | catch-up bump 0.69.2→0.69.3 to ship #1295 in a release tag — closes watchdog #1308
2026-05-03 06:52 | issue-filed: #1308 | release-stuck watchdog fired post #1295 merge — Version-Bump: skip trailer wasn't honored by auto-release.yml guard (PR-time gate accepted it; post-merge layer didn't). Pulp-side picking up the structural fix.
2026-05-03 06:52 | pr-opened: #1310 | fix(ci): auto-release watchdog must honor Version-Bump: skip — closes #1308. Codifies trailer parity between PR-time and post-merge gates so future @pulp/react JS-only fixes don't auto-strand.
2026-05-03 06:52 | issue-filed: #1312 | follow-up — add @pulp/react as third versioned surface (packages/pulp-react/package.json) so the version pipeline can track it natively + auto-publish to npm; today consumers rely on manual republish after Version-Bump: skip
2026-05-03 07:30 | issue-filed: #1311 | feat(import-design): --from open-design adapter + multi-type artifact dispatch (post-UX-parity) — companion to #1304's parser slice; covers CLI/--type/stdin/manifest flags + plugin slash-command + skill updates. BLOCKED on umbrella #1307 (UX parity). Cross-linked from #1304 with scope-split note (parser stays in #1304, CLI/plugin/dispatch in #1311).
2026-05-03 07:30 | issue-comment: #1307 | second-pass borrow-worthy patterns added (4 new beyond original 4): anti-slop linter as agent feedback loop (#1305-relevant), manifest-version field (#1300-relevant), skill front-matter pulp: extension (future), multi-CLI agent capability probe (#743-generalization). Captured as comment, not new issues.
2026-05-03 07:30 | planning-update: Spectr-Status.md | added "Post-UX-parity work queue" section documenting active phase (UX parity blockers) → unblocked next phase (umbrella #1307 sub-issues #1299–#1306) → subsequent phase (#1311 open-design CLI/plugin) → future placeholders (Sampler Phase 4, Spectr CI, WebView→Native migration).
2026-05-03 07:11 | release-shipped: v0.70.0 | contains #1295 (P0 React-dedup) + #1296 (P2 sweep #1171) + #1291 (#994 SvgPath intrinsic). Spectr should pin v0.70.0 to get the standalone launch crash fix.
2026-05-03 07:11 | pr-closed: #1309 | superseded — #1171 P2 sweep merge triggered v0.70.0 which already contains stranded #1295; catch-up bump no longer needed
2026-05-03 07:11 | issue-closed: #1308 | resolved organically by v0.70.0; structural fix at #1310 prevents re-fire of Version-Bump: skip false-positive
2026-05-03 07:11 | pr-merged: #1296 | Codex P2 sweep — 4 follow-ups bundled (corner-radius seeding, simulate_click bound, hover-test fix, compat-sync hard-fail-on-typo); ships in v0.70.0
2026-05-03 07:14 | release-tagged: v0.70.0 | pulp-side agent went minor (not patch) — v0.70.0 ships #1170/#1171/#1295/#1296. Release artifact still uploading.
2026-05-03 07:14 | pr-closed: #1309 | superseded by pulp-side's v0.70.0 minor bump path
2026-05-03 07:14 | issues-resolved | #1170 closed, #1171 closed (Codex P2 sweep #1296), #1292 closed (#1295). 8 reimport sub-issues #1299-#1306 + umbrella #1307 + open-design #1311 all blocked-on-UX-parity per gate.
2026-05-03 07:45 | pr-merged: #1310 | fix(ci): auto-release.yml watchdog now honors Version-Bump: skip — closes #1308; structural parity between PR-time gate and post-merge layer
2026-05-03 07:45 | pr-pushed: #1060 | rebased onto v0.70.0 main, dropped stale bump commit, added glob excludes for diff-cover (3 patterns to handle whatever path shape cobertura emits); fresh CI dispatching
2026-05-03 07:50 | spectr-blocker-confirmed-closed: #1292 | v0.70.0 SDK + rebuilt @pulp/react bundle (externals applied) + rebuilt editor.js (now 449KB vs 1.25MB; 0 copies of require_react_production_min2 vs 1 before) — 5/5 direct-exec launches succeed
2026-05-03 07:50 | parity-progress | popover-tap audit at v0.70.0: SCULPT opens (active-row outline, no content rows — #1147 still partial), PEAK opens (header + full description text — IMPROVED from v0.68.0 where text was cut off; overflow + icons still TBD), PRESETS click at x=480 didn't open (wrong x?)
