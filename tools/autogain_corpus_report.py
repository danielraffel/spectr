#!/usr/bin/env python3
"""Auto Gain v1 vs v2 corpus sweep and report (ADVISORY; not a ctest).

Run with the Audio Quality Lab environment (`pulp tool install audio-quality-lab`):

    ~/.pulp/tools/python-envs/audio-quality-lab/.venv/bin/python \\
        tools/autogain_corpus_report.py --build build --out <evidence dir>

It
  1. builds a license-clean corpus: Spectr's generated materials
     (Spectr-autogain-sweep --write-corpus) plus the quality-lab's own drum
     break, tonal and stereo-pad generators, each registered with provenance
     (license, SHA-256) through quality_lab.corpus.add_source;
  2. characterises each material with quality_lab.dsp.ltas (spectral
     centroid and the share of energy above 4 kHz / below 200 Hz);
  3. renders every material x shape through Spectr with AUTO v1 and v2 and
     measures BS.1770 integrated loudness of output vs input (LU error), the
     applied-gain movement and momentary-loudness spread (pumping);
  4. sweeps v2's time constant and compares the realised vs drawn response on
     a quick subset;
  5. writes results.jsonl, corpus/MANIFEST.json, and report.md under --out.

The fast deterministic subset of this lives in test/test_auto_gain_v2.cpp as
ctest gates; this script is the bigger, slower picture.
"""

from __future__ import annotations

import argparse
import json
import os
import statistics
import subprocess
import sys
from collections import defaultdict

import numpy as np
from quality_lab import audio_io, corpus, dsp, generate

SR = 48000

# name -> (material_class, family, narrow, flags)
SPECTR_MATERIALS = {
    "pink": ("noise", "noise", False, ""),
    "bass_line": ("bass", "tonal", True, ""),
    "vocal_buzz": ("vocal", "tonal", True, ""),
    "hats": ("percussive", "percussive", True, ""),
    "synth_pad": ("pad", "tonal", True, ""),
    "drum_loop_synth": ("percussive", "percussive", False, ""),
    "sine_1k": ("tonal", "tonal", True, ""),
    "pink_with_gaps": ("noise", "noise", False, ""),
    "freeze_bass_then_hats": ("bass", "tonal", True, "freeze@4.5"),
}


def tile(y: np.ndarray, n: int) -> np.ndarray:
    reps = int(np.ceil(n / len(y)))
    return np.concatenate([y] * reps)[:n]


def to_stereo(y: np.ndarray) -> np.ndarray:
    return y if y.ndim == 2 else np.stack([y, y], axis=1)


def write_f32(path: str, stereo: np.ndarray) -> None:
    np.asarray(stereo, dtype=np.float32).reshape(-1).tofile(path)


def read_f32(path: str) -> np.ndarray:
    return np.fromfile(path, dtype=np.float32).reshape(-1, 2)


def build_corpus(sweep: str, out: str) -> list[dict]:
    corpus_dir = os.path.join(out, "corpus")
    raw = os.path.join(corpus_dir, "raw")
    os.makedirs(raw, exist_ok=True)
    subprocess.run([sweep, "--write-corpus", raw], check=True, env=env())
    materials = []
    for name, (cls, family, narrow, flags) in SPECTR_MATERIALS.items():
        materials.append({"name": name, "f32": os.path.join(raw, f"{name}.f32"),
                          "class": cls, "family": family, "narrow": narrow, "flags": flags,
                          "generator": "Spectr-autogain-sweep --write-corpus "
                                       "(test/autogain_harness.hpp)"})
    n = 12 * SR
    ql = {
        "ql_drum_break": (to_stereo(tile(0.6 * generate.render_drum_break(SR, 120.0, 1.0, 0)[0], n)),
                          "percussive", "percussive", False,
                          "quality_lab.generate.render_drum_break(48000, 120, 1.0, seed=0), tiled"),
        "ql_tonal": (to_stereo(0.5 * generate.render_tonal(SR, 12.0, 0, 220.0)[0]),
                     "vocal", "tonal", True,
                     "quality_lab.generate.render_tonal(48000, 12 s, seed=0, f0=220)"),
        "ql_stereo_pad": (0.4 * generate.render_stereo_pad(SR, 12.0, 0, 220.0, 0.6),
                          "pad", "tonal", True,
                          "quality_lab.generate.render_stereo_pad(48000, 12 s, seed=0)"),
    }
    for name, (stereo, cls, family, narrow, gen) in ql.items():
        path = os.path.join(raw, f"{name}.f32")
        write_f32(path, stereo)
        materials.append({"name": name, "f32": path, "class": cls, "family": family,
                          "narrow": narrow, "flags": "", "generator": gen})
    # Register every source with provenance (license + SHA-256 of its WAV).
    for m in materials:
        wav = os.path.join(raw, f"{m['name']}.wav")
        audio_io.save_wav(wav, read_f32(m["f32"]), SR)
        entry = corpus.add_source(
            corpus_dir, wav, name=m["name"], material_class=m["class"],
            license_id="synthetic",
            expected=f"Auto Gain v2 loudness sweep material; generator: {m['generator']}",
            family=m["family"])
        m["sha256"] = entry["content_sha256"]
        y = read_f32(m["f32"]).mean(axis=1)
        f, mag = dsp.ltas(y, SR, n_fft=8192, hop=4096)
        power = mag ** 2
        total = float(power.sum()) or 1.0
        m["centroid_hz"] = float(dsp.spectral_centroid_hz(f, mag))
        m["share_above_4k"] = float(power[f >= 4000].sum() / total)
        m["share_below_200"] = float(power[f < 200].sum() / total)
    return materials


def env(extra: dict | None = None) -> dict:
    e = dict(os.environ)
    e["PULP_AUDIO_DEVICE"] = "null"
    e.pop("SPECTR_LEVEL_PLANT", None)
    e.pop("SPECTR_AUTOGAIN_TAU_S", None)
    if extra:
        e.update(extra)
    return e


def run_sweep(sweep: str, tsv: str, shapes: str, modes: str, extra: dict | None = None) -> list[dict]:
    proc = subprocess.run([sweep, "--corpus", tsv, "--shapes", shapes, "--modes", modes],
                          check=True, capture_output=True, text=True, env=env(extra))
    return [json.loads(line) for line in proc.stdout.splitlines() if line.startswith("{")]


def write_tsv(path: str, materials: list[dict]) -> None:
    with open(path, "w") as f:
        for m in materials:
            f.write(f"{m['name']}\t{m['f32']}\t{m['flags']}\n")


def pct(values: list[float], p: float) -> float:
    return float(np.percentile(np.asarray(values), p * 100.0)) if values else 0.0


def summarise(rows: list[dict], materials: list[dict]) -> dict:
    narrow = {m["name"] for m in materials if m["narrow"]}
    by_key = {(r["material"], r["shape"], r["mode"]): r for r in rows}
    out: dict = {}
    for mode in ("v1", "v2"):
        errs = [abs(r["error_lu"]) for r in rows if r["mode"] == mode]
        whole = [abs(r["error_whole_lu"]) for r in rows if r["mode"] == mode]
        out[mode] = {"n": len(errs), "p50": pct(errs, 0.5), "p95": pct(errs, 0.95),
                     "worst": max(errs) if errs else 0.0,
                     "whole_p95": pct(whole, 0.95), "whole_worst": max(whole) if whole else 0.0}
    better = worse = 0
    worse_cases = []
    for (mat, shape, mode), r in by_key.items():
        if mode != "v2" or mat not in narrow:
            continue
        v1 = by_key.get((mat, shape, "v1"))
        if v1 is None or abs(v1["error_lu"]) <= 0.5:
            continue
        if abs(r["error_lu"]) < abs(v1["error_lu"]):
            better += 1
        else:
            worse += 1
            worse_cases.append(f"{mat}/{shape}: v1 {v1['error_lu']:+.2f} v2 {r['error_lu']:+.2f}")
    out["narrow_better"] = better
    out["narrow_not_better"] = worse
    out["narrow_not_better_cases"] = worse_cases
    return out


def report(out: str, materials: list[dict], rows: list[dict], summary: dict,
           tau_rows: dict, drawn_rows: list[dict], realised_quick: list[dict]) -> str:
    L = []
    L.append("# Spectr Auto Gain v2 -- corpus sweep (advisory)\n")
    L.append("Loudness error = BS.1770 integrated loudness of the output with AUTO on minus "
             "the input's (Freeze: minus the same frozen render with AUTO off and a flat shape), "
             "over the steady window (from 4 s; 9 s for Freeze). `whole` starts at 0.5 s and "
             "includes v2's start-up from v1's estimate.\n")
    L.append("## Summary\n")
    L.append("| model | renders | median abs LU | p95 abs LU | worst abs LU | p95 incl. start-up | worst incl. start-up |")
    L.append("|---|---|---|---|---|---|---|")
    for mode in ("v1", "v2"):
        s = summary[mode]
        L.append(f"| {mode} | {s['n']} | {s['p50']:.2f} | {s['p95']:.2f} | {s['worst']:.2f} | "
                 f"{s['whole_p95']:.2f} | {s['whole_worst']:.2f} |")
    L.append(f"\nNarrow-material cases where v1 is off by > 0.5 LU: v2 better in "
             f"{summary['narrow_better']}, not better in {summary['narrow_not_better']}.")
    for c in summary["narrow_not_better_cases"]:
        L.append(f"- not better: {c}")
    L.append("\n## Corpus (license: synthetic; provenance in corpus/MANIFEST.json)\n")
    L.append("| material | class | narrow | centroid Hz | energy > 4 kHz | energy < 200 Hz | sha256 |")
    L.append("|---|---|---|---|---|---|---|")
    for m in materials:
        L.append(f"| {m['name']} | {m['class']} | {'yes' if m['narrow'] else 'no'} | "
                 f"{m['centroid_hz']:.0f} | {m['share_above_4k']:.4f} | {m['share_below_200']:.4f} | "
                 f"`{m['sha256'][:16]}` |")
    L.append("\n## LU error per material x shape (steady window)\n")
    by_mat = defaultdict(list)
    for r in rows:
        by_mat[r["material"]].append(r)
    for m in materials:
        mat_rows = by_mat.get(m["name"], [])
        if not mat_rows:
            continue
        L.append(f"### {m['name']}\n")
        L.append("| shape | AUTO off change | v1 error | v2 error | v2 error incl. start-up | v2 applied dB | v2 applied spread dB |")
        L.append("|---|---|---|---|---|---|---|")
        shapes = []
        for r in mat_rows:
            if r["shape"] not in shapes:
                shapes.append(r["shape"])
        for shape in shapes:
            v1 = next((r for r in mat_rows if r["shape"] == shape and r["mode"] == "v1"), None)
            v2 = next((r for r in mat_rows if r["shape"] == shape and r["mode"] == "v2"), None)
            if not v1 or not v2:
                continue
            L.append(f"| {shape} | {v2['off_change_lu']:+.2f} | {v1['error_lu']:+.2f} | "
                     f"{v2['error_lu']:+.2f} | {v2['error_whole_lu']:+.2f} | "
                     f"{v2['applied_db_end']:+.2f} | {v2['applied_spread_db']:.3f} |")
        L.append("")
    L.append("## Pumping (steady window; momentary loudness sd, AUTO off vs v2, same shape)\n")
    L.append("| material | shape | momentary sd off | momentary sd v2 | extra | v2 applied sd dB |")
    L.append("|---|---|---|---|---|---|")
    for r in rows:
        if r["mode"] != "v2" or r["shape"] not in ("low broad +12", "high broad -12"):
            continue
        L.append(f"| {r['material']} | {r['shape']} | {r['momentary_sd_off']:.3f} | "
                 f"{r['momentary_sd_on']:.3f} | {r['momentary_sd_on'] - r['momentary_sd_off']:+.3f} | "
                 f"{r['applied_sd_db']:.4f} |")
    L.append("\n## Time constant (quick subset)\n")
    L.append("| tau s | p95 abs LU | worst abs LU | p95 incl. start-up | mean applied sd dB | worst applied spread dB |")
    L.append("|---|---|---|---|---|---|")
    for tau, trs in sorted(tau_rows.items()):
        e = [abs(r["error_lu"]) for r in trs]
        w = [abs(r["error_whole_lu"]) for r in trs]
        sd = [r["applied_sd_db"] for r in trs]
        sp = [r["applied_spread_db"] for r in trs]
        L.append(f"| {tau} | {pct(e, 0.95):.2f} | {max(e):.2f} | {pct(w, 0.95):.2f} | "
                 f"{statistics.mean(sd):.4f} | {max(sp):.3f} |")
    L.append("\n## Realised vs drawn response (quick subset, v2)\n")
    L.append("| response | p95 abs LU | worst abs LU |")
    L.append("|---|---|---|")
    for label, rs in (("realised (shipping)", realised_quick), ("drawn bands", drawn_rows)):
        e = [abs(r["error_lu"]) for r in rs]
        if e:
            L.append(f"| {label} | {pct(e, 0.95):.2f} | {max(e):.2f} |")
    text = "\n".join(L) + "\n"
    with open(os.path.join(out, "report.md"), "w") as f:
        f.write(text)
    return text


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--build", default="build")
    ap.add_argument("--out", required=True)
    ap.add_argument("--skip-tau", action="store_true")
    args = ap.parse_args()
    sweep = os.path.join(args.build, "Spectr-autogain-sweep")
    if not os.path.exists(sweep):
        print(f"missing {sweep}: build target Spectr-autogain-sweep", file=sys.stderr)
        return 2
    os.makedirs(args.out, exist_ok=True)
    materials = build_corpus(sweep, args.out)
    tsv = os.path.join(args.out, "corpus.tsv")
    write_tsv(tsv, materials)
    rows = run_sweep(sweep, tsv, "all", "v1,v2")
    with open(os.path.join(args.out, "results.jsonl"), "w") as f:
        for r in rows:
            f.write(json.dumps(r) + "\n")
    summary = summarise(rows, materials)
    quick = [m for m in materials if m["name"] in
             ("drum_loop_synth", "pink", "bass_line", "vocal_buzz", "hats", "ql_drum_break")]
    quick_tsv = os.path.join(args.out, "corpus_quick.tsv")
    write_tsv(quick_tsv, quick)
    tau_rows: dict = {}
    if not args.skip_tau:
        for tau in ("1.0", "3.0", "6.0"):
            tau_rows[tau] = run_sweep(sweep, quick_tsv, "quick", "v2", {"SPECTR_AUTOGAIN_TAU_S": tau})
    realised_quick = run_sweep(sweep, quick_tsv, "quick", "v2")
    drawn = run_sweep(sweep, quick_tsv, "quick", "v2",
                      {"SPECTR_LEVEL_PLANT": "autogain-v2-drawn-response"})
    with open(os.path.join(args.out, "aux.json"), "w") as f:
        json.dump({"tau": tau_rows, "drawn": drawn, "realised_quick": realised_quick,
                   "summary": summary, "materials": materials}, f, indent=1)
    text = report(args.out, materials, rows, summary, tau_rows, drawn, realised_quick)
    print(text[:4000])
    return 0


if __name__ == "__main__":
    sys.exit(main())
