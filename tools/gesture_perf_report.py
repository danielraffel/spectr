#!/usr/bin/env python3
"""One consistent metric set per Spectr gesture-perf trace.

Reports frame-duration and frame-GAP distributions plus the per-slice costs
that matter for an interaction. Read the gap p95/p99 and the count of gaps at
or beyond 25 ms -- never the gap p50, which is pinned to the vsync interval
(~16.7 ms) in every arm, healthy or not, and so carries no information.

usage: gesture_perf_report.py a.pftrace [b.pftrace ...]   -> JSON on stdout

Each trace may sit beside a <name>.load file written by
tools/gesture_perf_capture.sh; when it does, the arm's machine load is
reported with its latencies, because one without the other is not a result.
"""
import json, os, subprocess, sys, pathlib, statistics, tempfile
# Perfetto's trace_processor. Overridable, and absent by default on a fresh
# machine: a hardcoded path that does not exist would make every trace report
# an empty metric set, which reads exactly like a fast editor.
TP = os.environ.get("PULP_TRACE_PROCESSOR") or str(
    pathlib.Path.home()
    / ".pulp/tools/trace-processor/v57.2/mac-arm64/trace_processor_shell")

SQL = r"""
CREATE OR REPLACE PERFETTO VIEW spectr_frames AS
SELECT ts, dur FROM slice WHERE category='render' AND name='frame' AND dur>=0;

CREATE OR REPLACE PERFETTO VIEW spectr_gaps AS
SELECT (LEAD(ts) OVER (ORDER BY ts) - ts)/1e6 AS gap_ms FROM spectr_frames;

CREATE OR REPLACE PERFETTO VIEW spectr_rank AS
SELECT name, dur/1e6 d,
       ROW_NUMBER() OVER (PARTITION BY name ORDER BY dur) rn,
       COUNT(*) OVER (PARTITION BY name) n2
FROM slice WHERE dur>=0 AND name IN (
  'native_drag_dispatch','redesign filter bank (UI thread)',
  'compile band mask table','shape band-edge transitions',
  'design minimum-phase impulse (FFT)','stage impulse for audio thread',
  'free retired impulse','dom_event_dispatch','frame_callback_pump',
  'raf_flush','paint','layout_children','js_native',
  'gesture perf sample','redesign filter bank (worker, audio-driven)');

SELECT r FROM (
  SELECT 0 o, 'R|frame_dur|'||n||'|'||p50||'|'||p95||'|'||p99||'|'||mx||'|0' r FROM (
    SELECT COUNT(*) n,
      CAST(MAX(CASE WHEN rn=CAST((50*n2+99)/100 AS INT) THEN d END) AS REAL) p50,
      CAST(MAX(CASE WHEN rn=CAST((95*n2+99)/100 AS INT) THEN d END) AS REAL) p95,
      CAST(MAX(CASE WHEN rn=CAST((99*n2+99)/100 AS INT) THEN d END) AS REAL) p99,
      CAST(MAX(d) AS REAL) mx
    FROM (SELECT dur/1e6 d, ROW_NUMBER() OVER (ORDER BY dur) rn,
                 COUNT(*) OVER () n2 FROM spectr_frames))
  UNION ALL
  SELECT 1, 'R|frame_gap|'||n||'|'||p50||'|'||p95||'|'||p99||'|'||mx||'|'||ge25 FROM (
    SELECT COUNT(*) n,
      CAST(MAX(CASE WHEN rn=CAST((50*n2+99)/100 AS INT) THEN d END) AS REAL) p50,
      CAST(MAX(CASE WHEN rn=CAST((95*n2+99)/100 AS INT) THEN d END) AS REAL) p95,
      CAST(MAX(CASE WHEN rn=CAST((99*n2+99)/100 AS INT) THEN d END) AS REAL) p99,
      CAST(MAX(d) AS REAL) mx, SUM(CASE WHEN d>=25 THEN 1 ELSE 0 END) ge25
    FROM (SELECT gap_ms d, ROW_NUMBER() OVER (ORDER BY gap_ms) rn,
                 COUNT(*) OVER () n2 FROM spectr_gaps WHERE gap_ms IS NOT NULL))
  UNION ALL
  SELECT 2, 'S|'||name||'|'||n||'|'||p50||'|'||p95||'|'||mx||'|'||tot FROM (
    SELECT name, COUNT(*) n,
      CAST(MAX(CASE WHEN rn=CAST((50*n2+99)/100 AS INT) THEN d END) AS REAL) p50,
      CAST(MAX(CASE WHEN rn=CAST((95*n2+99)/100 AS INT) THEN d END) AS REAL) p95,
      CAST(MAX(d) AS REAL) mx, CAST(SUM(d) AS REAL) tot
    FROM spectr_rank GROUP BY name)
  UNION ALL
  SELECT 3, 'T|'||COUNT(*)||'|'||CAST((MAX(ts+CASE WHEN dur<0 THEN 0 ELSE dur END)-MIN(ts))/1e6 AS REAL) FROM slice
  UNION ALL
  SELECT 4, 'L|'||name||'|'||value FROM stats WHERE severity='data_loss' AND value>0
  UNION ALL
  SELECT 5, 'U|'||COUNT(*) FROM slice WHERE dur=-1
) ORDER BY o;
"""

def run(trace):
    q = pathlib.Path(tempfile.gettempdir()) / "spectr-gesture-perf-query.sql"; q.write_text(SQL)
    if not pathlib.Path(TP).exists():
        raise SystemExit(f"no trace_processor at {TP}; set PULP_TRACE_PROCESSOR. "
                         "Reporting zeros from a missing tool is not a result.")
    out = subprocess.run([TP, "-q", str(q), trace], capture_output=True, text=True, timeout=600)
    res = {"frames": {}, "slices": {}, "total_slices": 0, "span_ms": 0,
           "data_loss": [], "unfinished": 0}
    for line in out.stdout.splitlines():
        line = line.strip().strip('"')
        p = line.split("|")
        if p[0] == "R":
            res["frames"][p[1]] = dict(n=int(p[2]), p50=float(p[3]), p95=float(p[4]),
                                       p99=float(p[5]), max=float(p[6]), ge25=int(float(p[7])))
        elif p[0] == "S":
            res["slices"][p[1]] = dict(n=int(p[2]), p50=float(p[3]), p95=float(p[4]),
                                       max=float(p[5]), total=float(p[6]))
        elif p[0] == "T":
            res["total_slices"], res["span_ms"] = int(p[1]), float(p[2])
        elif p[0] == "L": res["data_loss"].append((p[1], p[2]))
        elif p[0] == "U": res["unfinished"] = int(p[1])
    return res

if __name__ == "__main__":
    allr = {}
    for t in sys.argv[1:]:
        label = pathlib.Path(t).stem
        r = run(t)
        loadf = pathlib.Path(t).with_suffix(".load")
        if loadf.exists():
            v = [int(x) for x in loadf.read_text().split() if x.isdigit()]
            if v: r["load"] = dict(median=statistics.median(v), max=max(v), n=len(v))
        allr[label] = r
    print(json.dumps(allr, indent=1))
