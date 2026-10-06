#!/usr/bin/env python3
"""Freeze Length in a real host: REAPER renders Spectr offline and the looped
period is measured from the render.

For each case (tempo, meter, length) this builds a REAPER project with seeded
noise on one track through Spectr (VST3 or CLAP), automates Freeze on at a
downbeat, renders it OFFLINE (`REAPER -renderproject`), and measures the
period the frozen output repeats with: first found blind (the shortest lag at
which the held audio recurs, searched from 50 ms to 2.5x the expectation, so
a wrong length shows up as what it is), then refined to the sample. That must
equal bars x quarter-notes-per-bar x 60 / tempo within 1 ms, where a bar of
n/d is n x 4 / d quarter notes: REAPER's tempo, like the plugin transport,
counts quarter notes per minute, so 6/8 at 75 BPM is 3 quarters = 2.4 s a
bar. Lengths under 0.25 s are a spectral hold by design and are not timed.

Common lengths are selected with the Freeze Length parameter (automation, in
the lane's own range: 0..1 for VST3, the plain 0..20 for CLAP). Custom lengths
are written into the plugin's own saved state inside the project (the Pulp
PLST envelope: the CLAP <STATE> block, or the component state inside REAPER's
VST3 chunk), the way a saved session carries them. Save/reopen: one REAPER
session opens the 120 4/4, 2 3/16-bar case, chooses Custom and saves; a
second opens that file cold, reports what the plugin shows and saves again;
that file's state is read back and rendered, and must match the first render.

No audio device: REAPER on macOS has no Dummy Audio system, so the session's
CoreAudio input and output name a device that does not exist; REAPER then
starts with none open (pass A asserts it) and nothing can reach a speaker.
Zero pollution, by Pulp's daw-smoke ReaperSession: a fresh portable REAPER
resource directory (seeded from a copy of the user's settings, never written
back) and a temporary scan folder, both removed on exit; every REAPER this
starts is capped by --timeout and stopped. AU is not covered: REAPER finds an
AU only through the system registry, which needs an install into
~/Library/Audio/Plug-Ins/Components.

Usage:
  tools/reaper_freeze_length.py --plugin build/CLAP/Spectr.clap \
      --pulp-repo ~/Code/pulp [--out DIR] [--keep]
  (likewise --plugin build/VST3/Spectr.vst3). Needs numpy.
Exit: 0 every case within tolerance, 1 a case out of tolerance or a failed
render, 2 REAPER missing (SKIP -- not a pass).
"""
from __future__ import annotations

import argparse
import base64
import importlib.util
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import wave
import zlib
from fractions import Fraction
from pathlib import Path

import numpy as np

SR = 48000
LOOP_MIN_SECONDS = 0.25  # FreezeSource::kLoopMinSeconds: shorter holds are spectral
TOLERANCE_MS = 1.0

PASS_A_LUA = r'''
local out = os.getenv("SPECTR_LEN_OUT")
local status = os.getenv("PULP_DAW_SMOKE_STATUS")
reaper.InsertTrackAtIndex(0, true)
local tr = reaper.GetTrack(0, 0)
local want = os.getenv("PULP_DAW_SMOKE_FX")
local fx = -1
local spaced = (want:gsub(":", ": "))
for _, name in ipairs({want, spaced .. " (Pulp)", spaced}) do
  fx = reaper.TrackFX_AddByName(tr, name, false, -1)
  if fx >= 0 then break end
end
local s = io.open(status, "w")
if fx < 0 then
  local names = {}
  local i = 0
  while true do
    local ok, name = reaper.EnumInstalledFX(i)
    if not ok then break end
    if name:find("Spectr") then names[#names + 1] = name end
    i = i + 1
  end
  s:write("FX_NOT_FOUND installed=" .. table.concat(names, ";") .. " of " .. i); s:close(); return
end
local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
if not fxname:find("Spectr") then s:write("FX_NOT_FOUND wrong fx " .. fxname); s:close(); return end
local f = io.open(out .. "/params.txt", "w")
local freeze, length = -1, -1
for i = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do
  local _, name = reaper.TrackFX_GetParamName(tr, fx, i, "")
  f:write(i .. "\t" .. name .. "\n")
  if name == "Freeze" then freeze = i end
  if name == "Freeze Length" then length = i end
end
f:close()
-- Freeze: off, then on at a time the driver rewrites per case.
local env = reaper.GetFXEnvelope(tr, fx, freeze, true)
reaper.InsertEnvelopePoint(env, 0.0, 0.0, 1, 0, false, true)
reaper.InsertEnvelopePoint(env, 7.777, 1.0, 1, 0, false, true)
reaper.Envelope_SortPoints(env)
-- Freeze Length: one point, its value rewritten per case.
local lenv = reaper.GetFXEnvelope(tr, fx, length, true)
reaper.InsertEnvelopePoint(lenv, 0.0, 0.0, 1, 0, false, true)
reaper.Envelope_SortPoints(lenv)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", out, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "render", true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.Main_SaveProjectEx(0, out .. "/base.rpp", 0)
local dev_open, dev = reaper.GetAudioDeviceInfo("IDENT_OUT")
s:write("FX_SHOWN freeze=" .. freeze .. " length=" .. length
        .. " device=" .. (dev_open and dev or "none"))
s:close()
'''

RESAVE_LUA = r'''
-- The project is open (REAPER loaded it, and the plugin its state). Report
-- what the plugin says its Freeze Length is, then save the project again:
-- the state REAPER writes is the reloaded plugin's own serialisation.
-- With SPECTR_LEN_SET_CUSTOM set, first choose "Custom" on the parameter,
-- as the header's dropdown does when a length is not one of its presets.
local out = os.getenv("SPECTR_LEN_RESAVE")
local tr = reaper.GetTrack(0, 0)
local label, value = "?", -1
if tr then
  for i = 0, reaper.TrackFX_GetNumParams(tr, 0) - 1 do
    local _, name = reaper.TrackFX_GetParamName(tr, 0, i, "")
    if name == "Freeze Length" then
      if os.getenv("SPECTR_LEN_SET_CUSTOM") == "1" then
        reaper.TrackFX_SetParamNormalized(tr, 0, i, 1.0)
        -- A CLAP plugin takes a host parameter change in its next process
        -- call, and this session has no audio device, so nothing processes
        -- while stopped. An offline render (to a scratch file) runs it.
        reaper.GetSetProjectInfo_String(0, "RENDER_FILE", os.getenv("SPECTR_LEN_SCRATCH"), true)
        reaper.Main_OnCommand(42230, 0) -- render with the project's settings, auto-close
      end
      _, label = reaper.TrackFX_GetFormattedParamValue(tr, 0, i, "")
      value = reaper.TrackFX_GetParamNormalized(tr, 0, i)
    end
  end
end
reaper.Main_SaveProjectEx(0, out, 0)
local s = io.open(os.getenv("PULP_DAW_SMOKE_STATUS"), "w")
s:write("FX_SHOWN resaved param_label=" .. label .. " param_norm=" .. value)
s:close()
'''


def log(msg):
    print(f"[reaper-length] {msg}", flush=True)


NO_DEVICE = "spectr-length-no-device"


def silence_audio_device(ini: Path) -> None:
    """Point the session's CoreAudio input and output at a device that does
    not exist, and close any device while stopped.

    REAPER on macOS offers no Dummy Audio system, and the session config is
    seeded from the user's own (licence, preferences), so it would open their
    default output. Naming a device that is not there makes REAPER start with
    NO device open (GetAudioDeviceInfo reports nothing), with no dialog; an
    offline render needs none. The section header is matched without regard
    to case: a real reaper.ini writes it as `[reaper]`."""
    text = ini.read_text()
    ours = {"coreaudiooutdevnew": NO_DEVICE, "coreaudioindevnew": NO_DEVICE,
            "audioclosestop": "1", "audiocloseinactive": "1"}
    for key in ours:
        text = re.sub(rf"(?m)^{key}=.*\n", "", text)
    lines = "".join(f"{k}={v}\n" for k, v in ours.items())
    text, n = re.subn(r"(?im)^\[reaper\]\n", lambda m: m.group(0) + lines, text, count=1)
    if n != 1:
        text = "[REAPER]\n" + lines + text
    ini.write_text(text)


SCANNED = {
    # A finished scan names the plugin; an interrupted one leaves the bundle
    # with a timestamp and nothing else ("Spectr.vst3=00B0..."), which the
    # shared smoke's readiness check (any line mentioning the name) accepts.
    "vst3": re.compile(r"(?m)^Spectr\.vst3=[^\n]*,[^\n]*,Spectr"),
    "clap": re.compile(r"(?m)^com\.[^\n=]*=\d+\|Spectr"),
}


def warm_scan(reaper: Path, ini: Path, fmt: str, timeout: float) -> bool:
    """Launch REAPER once and wait until its cache holds a FINISHED scan of
    the plugin, then close it. Without this the scripted launch can come up
    with the scan half done and insert nothing."""
    pattern = SCANNED[fmt]
    glob = "reaper-vstplugins*.ini" if fmt == "vst3" else "reaper-clap-*.ini"
    proc = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    deadline, found = time.time() + timeout, False
    try:
        while time.time() < deadline and not found:
            time.sleep(1.0)
            for cache in ini.parent.glob(glob):
                if pattern.search(cache.read_text(errors="replace")):
                    found = True
        time.sleep(1.0)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)
    return found


def load_reaper_smoke(pulp_repo: Path):
    path = pulp_repo / "tools" / "testing" / "daw-smoke" / "reaper_smoke.py"
    spec = importlib.util.spec_from_file_location("reaper_smoke", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_noise(path: Path, seconds: float, seed: int = 5):
    rng = np.random.default_rng(seed)
    data = (rng.uniform(-0.25, 0.25, size=(int(seconds * SR), 2))).astype(np.float32)
    pcm = (np.clip(data, -1, 1) * 32767).astype("<i2")
    with wave.open(str(path), "wb") as w:
        w.setnchannels(2)
        w.setsampwidth(2)
        w.setframerate(SR)
        w.writeframes(pcm.tobytes())


def read_wav(path: Path) -> np.ndarray:
    with wave.open(str(path), "rb") as w:
        n, ch, width = w.getnframes(), w.getnchannels(), w.getsampwidth()
        raw = w.readframes(n)
    if width == 2:
        x = np.frombuffer(raw, "<i2").astype(np.float64) / 32768.0
    elif width == 3:
        b = np.frombuffer(raw, np.uint8).reshape(-1, 3)
        v = (b[:, 0].astype(np.int32) | (b[:, 1].astype(np.int32) << 8)
             | (b[:, 2].astype(np.int32) << 16))
        v = np.where(v >= 1 << 23, v - (1 << 24), v)
        x = v.astype(np.float64) / float(1 << 23)
    elif width == 4:
        x = np.frombuffer(raw, "<f4").astype(np.float64)
    else:
        raise RuntimeError(f"unsupported wav width {width}")
    return x.reshape(-1, ch)[:, 0]


# ── the plugin state inside the project ─────────────────────────────────────

# REAPER keeps a plugin's state as base64 lines inside its FX block, wrapped
# at 128 characters (96 bytes) per line, each line decodable on its own.
#
#   CLAP: a <STATE ... > sub-block holding the plugin's state bytes as is
#         (here, the Pulp PLST envelope).
#   VST3: the lines straight after the <VST ...> header line. Decoded and
#         joined they are REAPER's header (magic, pin maps, then the size of
#         the data that follows), then the data: a u32 size + u32 flag before
#         the component state (the PLST envelope), then the controller state,
#         then a short trailer of REAPER's own.
# The PLST envelope: "PLST", u32 version, u32 store size, u32 plugin size,
# the StateStore blob, the plugin's own JSON, u32 CRC-32 over all before it.

B64_LINE = re.compile(r"^[A-Za-z0-9+/=]+$")


def _fx_block(rpp: str):
    """(start, end) of the Spectr FX block's state lines, and the format."""
    m = re.search(r'\n(\s*)<CLAP "CLAP: Spectr[^\n]*\n', rpp)
    if m:
        st = re.compile(r"\n(\s*)<STATE\n").search(rpp, m.end() - 1)
        if not st:
            raise RuntimeError("no <STATE> in the Spectr CLAP block")
        close = re.compile(r"\n\s*>").search(rpp, st.end() - 1)
        return "clap", st.end(), close.start() + 1
    m = re.search(r'\n\s*<VST "VST3: Spectr[^\n]*\n', rpp)
    if not m:
        raise RuntimeError("no Spectr FX block in the project")
    start = m.end()
    pos = start
    while True:
        nl = rpp.index("\n", pos)
        if not B64_LINE.match(rpp[pos:nl].strip()):
            return "vst3", start, pos
        pos = nl + 1


def _decode_lines(text: str):
    lines = [l for l in text.splitlines() if l.strip()]
    indent = re.match(r"\s*", lines[0]).group(0)
    return indent, b"".join(base64.b64decode(l.strip()) for l in lines)


def _encode(indent: str, *sections: bytes) -> str:
    out = []
    for sec in sections:
        enc = base64.b64encode(sec).decode()
        out += [indent + enc[i:i + 128] for i in range(0, len(enc), 128)]
    return "\n".join(out) + "\n"


def _envelope(blob: bytes, at: int):
    if blob[at:at + 4] != b"PLST":
        raise RuntimeError("no PLST envelope in the plugin state")
    store, plugin = struct.unpack_from("<II", blob, at + 8)
    end = at + 16 + store + plugin + 4
    crc = struct.unpack_from("<I", blob, end - 4)[0]
    if crc != zlib.crc32(blob[at:end - 4]) & 0xFFFFFFFF:
        raise RuntimeError("PLST CRC does not match")
    doc = json.loads(blob[at + 16 + store: at + 16 + store + plugin])
    return store, end, doc


def _vst3_split(blob: bytes):
    """REAPER's header, the data (size-prefixed component state + the rest)
    and its trailer, as recorded by the header's data-size field."""
    at = blob.find(b"PLST")
    if at < 8:
        raise RuntimeError("no PLST envelope in the VST3 state")
    data_start = at - 8
    comp_size = struct.unpack_from("<I", blob, data_start)[0]
    header = blob[:data_start]
    # The header's data size is the u32 three from its end (then a flag
    # and a 0xFFFF marker); check it covers the component state.
    data_size = struct.unpack_from("<I", header, len(header) - 12)[0]
    if not comp_size + 8 <= data_size <= len(blob) - data_start:
        raise RuntimeError("REAPER's VST3 header does not frame the state")
    data = blob[data_start:data_start + data_size]
    return header, data, blob[data_start + data_size:]


def read_state(rpp: str) -> dict:
    """The Spectr plugin JSON stored in the project."""
    fmt, a, b = _fx_block(rpp)
    _, blob = _decode_lines(rpp[a:b])
    at = blob.find(b"PLST") if fmt == "vst3" else 0
    return _envelope(blob, at)[2]


def read_custom_length(rpp: str):
    return read_state(rpp).get("freeze_length")


def write_custom_length(rpp: str, bars: int, fraction: str) -> str:
    """Rewrite the custom length in the project's saved Spectr state."""
    fmt, a, b = _fx_block(rpp)
    indent, blob = _decode_lines(rpp[a:b])
    if fmt == "vst3":
        header, data, trailer = _vst3_split(blob)
        env_at = 8
    else:
        header, data, trailer = b"", blob, b""
        env_at = 0
    store, end, doc = _envelope(data, env_at)
    doc["freeze_length"] = {"bars": bars, "fraction": fraction}
    plugin_json = json.dumps(doc, separators=(",", ":")).encode()
    env = bytearray(data[env_at:env_at + 8])
    env += struct.pack("<II", store, len(plugin_json))
    env += data[env_at + 16: env_at + 16 + store] + plugin_json
    env += struct.pack("<I", zlib.crc32(bytes(env)) & 0xFFFFFFFF)
    if fmt == "vst3":
        rest = data[end:]
        new_data = struct.pack("<I", len(env)) + data[4:8] + bytes(env) + rest
        h = bytearray(header)
        struct.pack_into("<I", h, len(h) - 12, len(new_data))
        text = _encode(indent, bytes(h), new_data, trailer)
    else:
        text = _encode(indent, bytes(env) + data[end:])
    return rpp[:a] + text + rpp[b:]


def selftest_state(rpp: str) -> None:
    """A rewrite to the length already stored must reproduce REAPER's own
    lines byte for byte, except the JSON's whitespace; and the new length
    must read back."""
    fmt, a, b = _fx_block(rpp)
    _, blob = _decode_lines(rpp[a:b])
    if fmt == "vst3":
        header, data, trailer = _vst3_split(blob)
        if _encode(_decode_lines(rpp[a:b])[0], header, data, trailer) != rpp[a:b]:
            raise RuntimeError("VST3 state re-encode does not round-trip REAPER's lines")
    out = write_custom_length(rpp, 2, "3/16")
    if read_custom_length(out) != {"bars": 2, "fraction": "3/16"}:
        raise RuntimeError("custom length did not read back")


# ── per-case projects ───────────────────────────────────────────────────────

def _envelope_value(header: str, preset: int) -> float:
    """The automation value for a Freeze Length preset index, in the lane's
    own range: REAPER writes a VST3 lane 0..1 (normalised) and a CLAP lane
    in the parameter's plain range (0..20 here)."""
    lo, hi = (float(v) for v in header.split()[1:3])
    plain_max = 20.0  # kLengthPresetCustom
    return lo + (hi - lo) * preset / plain_max if hi <= 1.0 else float(preset)


def make_case(base: str, wav: Path, seconds: float, bpm: float, num: int, den: int,
              engage: float, preset: int, render_to: Path, tempo_change=None) -> str:
    rpp, n = re.subn(r"\n(\s*)TEMPO [^\n]*",
                     lambda m: f"\n{m.group(1)}TEMPO {bpm} {num} {den} 0", base, count=1)
    if n != 1:
        raise RuntimeError("no TEMPO line in the project")
    if tempo_change is not None:
        # Points on the project's own tempo envelope (a second TEMPOENVEX
        # block would be a second envelope). Square shape: the tempo steps.
        t, new_bpm = tempo_change
        points = (f"    PT 0 {bpm} 1 {num + den * 65536}\n"
                  f"    PT {t} {new_bpm} 1\n")
        rpp, n = re.subn(r"(<TEMPOENVEX\n(?:\s+[A-Z][^\n]*\n)*?)(\s*>)",
                         lambda m: m.group(1) + points + m.group(2), rpp, count=1)
        if n != 1:
            raise RuntimeError("no tempo envelope in the project")
    rpp, n = re.subn(r'RENDER_FILE "[^"]*"', f'RENDER_FILE "{render_to}"', rpp, count=1)
    if n != 1:
        raise RuntimeError("no RENDER_FILE in the project")
    envs = list(re.finditer(r"<PARMENV ([^\n]*)\n(.*?)\n\s*>", rpp, re.S))
    names = [e.group(1) for e in envs]
    if len(envs) != 2 or '"Freeze /' not in names[0] or '"Freeze Length /' not in names[1]:
        raise RuntimeError(f"expected the Freeze and Freeze Length lanes, found {names}")
    freeze_env, length_env = envs
    value = _envelope_value(length_env.group(1), preset)
    new_length = re.sub(r"PT 0 [0-9.e-]+", f"PT 0 {value:.6f}", length_env.group(0))
    new_freeze = re.sub(r"PT 7\.777\d*", f"PT {engage:.6f}", freeze_env.group(0))
    rpp = (rpp[:freeze_env.start()] + new_freeze + rpp[freeze_env.end():length_env.start()]
           + new_length + rpp[length_env.end():])
    item = (f"    <ITEM\n      POSITION 0\n      LENGTH {seconds}\n      LOOP 0\n"
            f"      <SOURCE WAVE\n        FILE \"{wav}\"\n      >\n    >\n")
    # The item goes at the end of the (only) track.
    track_end = rpp.rfind("\n  >")
    rpp = rpp[:track_end + 1] + item + rpp[track_end + 1:]
    return rpp


def first_repeat(y: np.ndarray, start: int, win: int, max_lag: int) -> float | None:
    """The SHORTEST lag (s) at which the window starting at `start` recurs
    (normalised correlation > 0.9), searched blind from 50 ms to `max_lag`
    samples. Independent of the expected period, so a loop of the wrong
    length -- or a multiple or a fraction of the right one -- shows up as
    what it is rather than as a weak peak near the expectation."""
    a = y[start:start + win]
    seg = y[start:start + max_lag + win]
    n = 1 << int(math.ceil(math.log2(len(seg) + win)))
    xc = np.fft.irfft(np.fft.rfft(seg, n) * np.conj(np.fft.rfft(a, n)), n)[:max_lag + 1]
    csum = np.concatenate([[0.0], np.cumsum(seg * seg)])
    energy = csum[win:win + max_lag + 1] - csum[:max_lag + 1]
    norm = xc / np.sqrt(np.maximum(energy * float(np.dot(a, a)), 1e-30))
    lo = int(0.05 * SR)
    hits = np.nonzero(norm[lo:] > 0.9)[0]
    if not len(hits):
        return None
    k = lo + int(hits[0])
    # The local peak of that first excursion above 0.9.
    while k + 1 < len(norm) and norm[k + 1] > norm[k]:
        k += 1
    return k / SR


def measure_period(y: np.ndarray, engage_sample: int, expected: float):
    """The period the held audio repeats with: the first recurrence found
    blind (first_repeat), then refined to the sample -- normalised
    correlation of a 0.4 s window just after the engage fade at every lag
    within 2 ms of it, and a parabola through the peak. Returns (period,
    correlation, blind period)."""
    start = engage_sample + int(0.35 * SR)
    win = int(0.4 * SR)
    max_lag = int(2.5 * expected * SR)
    if len(y) < start + max_lag + win:
        raise RuntimeError("render too short for the measurement")
    blind = first_repeat(y, start, win, max_lag)
    if blind is None:
        return None, 0.0, None
    centre = int(round(blind * SR))
    a = y[start:start + win]
    best, best_lag, scores = -2.0, centre, {}
    for lag in range(centre - int(0.002 * SR), centre + int(0.002 * SR) + 1):
        b = y[start + lag:start + lag + win]
        c = float(np.dot(a, b) / math.sqrt(max(np.dot(a, a) * np.dot(b, b), 1e-30)))
        scores[lag] = c
        if c > best:
            best, best_lag = c, lag
    l, r = scores.get(best_lag - 1), scores.get(best_lag + 1)
    frac = 0.0
    if l is not None and r is not None and (l - 2 * best + r) != 0:
        frac = 0.5 * (l - r) / (l - 2 * best + r)
    return (best_lag + frac) / SR, best, blind


def render(reaper: Path, ini: Path, proj: Path, env: dict, timeout: float) -> None:
    """Render a project OFFLINE (`-renderproject`: REAPER renders the whole
    project to its RENDER_FILE as fast as it can and quits; nothing is sent
    to a device, and the session has none open). Capped, and the REAPER this
    started is always stopped."""
    proc = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash",
                             "-ignoreerrors", "-renderproject", str(proj)],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        log(f"render of {proj.name} exceeded {timeout:.0f} s; stopping REAPER")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)


def length_label(bars: int, fraction: str) -> str:
    """freeze_length.hpp's length_label: "1 bar", "1 1/8 bars", "1/8 bar"."""
    total = Fraction(bars) + (Fraction(fraction) if fraction != "0" else 0)
    unit = " bar" if total <= 1 else " bars"
    if fraction == "0":
        return f"{bars}{unit}"
    return (f"{fraction}" if bars == 0 else f"{bars} {fraction}") + unit


def run(args) -> int:
    smoke = load_reaper_smoke(Path(args.pulp_repo).expanduser())
    reaper = smoke.find_reaper()
    if reaper is None:
        log("REAPER not installed -- SKIP (not a pass)")
        return 2
    out = Path(args.out)
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    plugin = Path(args.plugin).resolve()
    fmt = "clap" if plugin.suffix == ".clap" else "vst3"
    ns = argparse.Namespace(plugin_path=str(plugin), plugin_name="Spectr", format=fmt,
                            timeout=args.timeout)
    session = smoke.ReaperSession(reaper, ns)
    try:
        if (code := session.place_plugin()) is not None:
            return code
        ini = session.portable / "reaper.ini"
        silence_audio_device(ini)
        if not warm_scan(reaper, ini, fmt, args.timeout):
            log("REAPER never finished scanning the plugin -- INCONCLUSIVE")
            return 1
        status = session.portable / "status.txt"
        env = dict(os.environ, PULP_DAW_SMOKE_FX=("CLAP:" if fmt == "clap" else "VST3:") + "Spectr",
                   PULP_DAW_SMOKE_STATUS=str(status), SPECTR_LEN_OUT=str(out))
        lua = session.portable / "pass_a.lua"
        lua.write_text(PASS_A_LUA)
        if (code := session.run_until_fx_shown(env, status, lua)) is not None:
            log("pass A did not finish: " + (status.read_text() if status.exists() else "no status"))
            for cache in list(session.portable.glob("reaper-*.ini")) + list(session.portable.glob("*.txt")):
                shutil.copy2(cache, out / cache.name)
            return 1
        time.sleep(1.0)
        session.terminate()
        base = (out / "base.rpp").read_text()
        pass_a = status.read_text()
        log("pass A: " + pass_a)
        if "device=none" not in pass_a:
            log("REAPER opened an audio device; refusing to render -- FAIL")
            return 1
        selftest_state(base)

        wav = out / "noise.wav"
        write_noise(wav, 40.0)

        F = Fraction
        # (label, bpm, num, den, bars as Fraction, how)
        cases = []
        for bpm, num, den in ((120, 4, 4), (90, 4, 4), (140, 3, 4), (75, 6, 8)):
            for bars, frac in ((0, "1/8"), (1, "0"), (2, "0"), (1, "1/8"), (2, "3/16"), (0, "1/12")):
                cases.append((bpm, num, den, bars, frac, None))
        cases.append((120, 4, 4, 1, "0", (6.0, 90)))     # tempo change before engage
        cases.append((140, 3, 4, 1, "1/8", (6.0, 100)))
        # kLengthPresets: the sixteen fractions of a bar alone (0..15), then
        # 1, 2, 4 and 8 bars (16..19); 20 is Custom.
        fractions = ["1/32", "1/16", "1/12", "1/8", "1/6", "3/16", "1/4", "1/3", "3/8",
                     "1/2", "5/8", "2/3", "3/4", "5/6", "7/8", "15/16"]
        presets = {(0, f): i for i, f in enumerate(fractions)}
        presets.update({(1, "0"): 16, (2, "0"): 17, (4, "0"): 18, (8, "0"): 19})

        rows, worst = [], 0.0
        failures = 0
        engages, periods = {}, {}
        for i, (bpm, num, den, bars, frac, change) in enumerate(cases):
            total = F(bars) + (F(frac) if frac != "0" else F(0))
            qpb = F(num * 4, den)
            final_bpm = change[1] if change else bpm
            expected = float(total * qpb * 60 / final_bpm)
            bar_seconds = float(qpb * 60 / final_bpm)
            # Engage on a downbeat: at least four bars in (and a loop's worth
            # of input heard), or three bars after a tempo change.
            if change:
                engage = change[0] + 3 * bar_seconds
            else:
                bars_in = max(4, math.ceil((expected + 2.0) / bar_seconds))
                engage = bars_in * bar_seconds
            preset = presets.get((bars, frac))
            proj = out / f"case{i:02d}.rpp"
            rendered = out / f"case{i:02d}.wav"
            rpp = make_case(base, wav, 40.0, bpm, num, den, engage,
                            preset if preset is not None else 20, rendered, change)
            if preset is None:
                rpp = write_custom_length(rpp, bars, frac)
            proj.write_text(rpp)
            engages[i] = engage
            render(reaper, ini, proj, env, args.timeout)
            candidates = [rendered] if rendered.exists() else []
            label = length_label(bars, frac)
            where = f"{bpm} {num}/{den}" + (f" -> {change[1]} at {change[0]}s" if change else "")
            how = "preset (automation)" if preset is not None else "custom (state)"
            if not candidates:
                rows.append((where, label, how, expected, None, None, "NO RENDER"))
                failures += 1
                continue
            y = read_wav(candidates[0])
            if expected < LOOP_MIN_SECONDS:
                rows.append((where, label, how, expected, None, None,
                             "spectral hold (< 0.25 s, no loop by design)"))
                continue
            period, corr, _ = measure_period(y, int(engage * SR), expected)
            if period is None:
                failures += 1
                rows.append((where, label, how, expected, None, None, "FAIL (no repeat found)"))
                continue
            err_ms = (period - expected) * 1000.0
            ok = abs(err_ms) <= TOLERANCE_MS and corr > 0.95
            failures += 0 if ok else 1
            worst = max(worst, abs(err_ms))
            periods[i] = period
            rows.append((where, label, how, expected, period, corr,
                         ("PASS" if ok else "FAIL") + f" ({err_ms:+.3f} ms)"))

        # Save / reopen: REAPER loads a custom-length project and saves it
        # again; the length must come back out of REAPER's own file, and that
        # file must render the same period.
        # Save / reopen. Session 1 opens the custom-length case, chooses
        # "Custom" on the parameter (stopped, so the value is the plugin's
        # own, not the automation's) and saves. Session 2 opens THAT file
        # cold, reports what the plugin shows, and saves again: the state in
        # the second file is the reopened plugin's own serialisation. The
        # second file is then rendered.
        src_i = cases.index((120, 4, 4, 2, "3/16", None))
        src = out / f"case{src_i:02d}.rpp"
        saved = out / "saved.rpp"
        resaved = out / "resaved.rpp"
        lua2 = session.portable / "resave.lua"
        lua2.write_text(RESAVE_LUA)

        def reopen(project: Path, dest: Path, set_custom: bool) -> str:
            status.unlink(missing_ok=True)
            env2 = dict(env, SPECTR_LEN_RESAVE=str(dest),
                        SPECTR_LEN_SCRATCH=str(out / "scratch"),
                        SPECTR_LEN_SET_CUSTOM="1" if set_custom else "0")
            proc = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash",
                                     "-ignoreerrors", str(project), str(lua2)], env=env2,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            try:
                deadline = time.time() + args.timeout
                while time.time() < deadline and not (
                        status.exists() and "resaved" in status.read_text()):
                    time.sleep(1)
                time.sleep(1.0)
            finally:
                smoke.stop_reaper_process(proc)
            return status.read_text().strip() if status.exists() else "no status"

        log("save: " + reopen(src, saved, True))
        reopen_status = reopen(saved, resaved, False) if saved.exists() else "not saved"
        log("reopen: " + reopen_status)
        if resaved.exists():
            roundtrip = read_custom_length(resaved.read_text())
            restored = (length_label(roundtrip["bars"], roundtrip["fraction"])
                        if roundtrip else "none")
            again = out / "resaved.wav"
            rr = re.sub(r'RENDER_FILE "[^"]*"', f'RENDER_FILE "{again}"', resaved.read_text(), count=1)
            (out / "resaved_render.rpp").write_text(rr)
            render(reaper, ini, out / "resaved_render.rpp", env, args.timeout)
            expected = float((F(2) + F(3, 16)) * 4 * 60 / 120)
            if again.exists():
                y2 = read_wav(again)
                period, corr, _ = measure_period(y2, int(engages[src_i] * SR), expected)
                period = period if period is not None else float("nan")
                err_ms = (period - expected) * 1000.0
                same = abs(period - periods.get(src_i, -1.0)) * 1000.0
                first = out / f"case{src_i:02d}.wav"
                y1 = read_wav(first)
                n = min(len(y1), len(y2))
                diff = float(np.max(np.abs(y1[:n] - y2[:n]))) if n else float("nan")
                ok = (roundtrip == {"bars": 2, "fraction": "3/16"} and "param_label=Custom" in reopen_status
                      and abs(err_ms) <= TOLERANCE_MS and corr > 0.95 and same <= 0.01)
                failures += 0 if ok else 1
                rows.append(("120 4/4 (saved, reopened, re-saved)", "2 3/16 bars",
                             f"restored {roundtrip} = \"{restored}\"; host shows "
                             + reopen_status.split("param_label=")[-1].split(" ")[0],
                             expected, period, corr,
                             ("PASS" if ok else "FAIL") + f" ({err_ms:+.3f} ms; vs first render "
                             f"{same:.4f} ms, max sample diff {diff:.2e})"))
            else:
                failures += 1
                rows.append(("120 4/4 (saved, reopened, re-saved)", "2 3/16 bars", str(roundtrip),
                             expected, None, None, "NO RENDER"))
        else:
            failures += 1
            rows.append(("save/reopen", "-", "-", 0, None, None, "REAPER did not re-save"))

        print("\n| transport | length | set by | expected (s) | measured (s) | corr | result |")
        print("|---|---|---|---:|---:|---:|---|")
        for where, label, how, exp, per, corr, res in rows:
            print(f"| {where} | {label} | {how} | {exp:.6f} | "
                  + (f"{per:.6f}" if per is not None else "-") + " | "
                  + (f"{corr:.4f}" if corr is not None else "-") + f" | {res} |")
        version = subprocess.run(["defaults", "read", str(reaper.parents[1] / "Info.plist"),
                                  "CFBundleShortVersionString"], capture_output=True, text=True)
        log(f"{fmt.upper()} in REAPER {version.stdout.strip() or '?'}: worst error "
            f"{worst:.3f} ms; {failures} failure(s)")
        return 1 if failures else 0
    finally:
        session.cleanup()
        if not args.keep:
            shutil.rmtree(out, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--plugin", required=True, help="Spectr.vst3 or Spectr.clap to load")
    ap.add_argument("--pulp-repo", default="~/Code/pulp", help="Pulp checkout (daw-smoke harness)")
    ap.add_argument("--out", default=None, help="work directory (default: a new temp dir)")
    ap.add_argument("--timeout", type=int, default=120,
                    help="cap, in seconds, on each REAPER launch")
    ap.add_argument("--keep", action="store_true", help="keep the projects and renders")
    args = ap.parse_args()
    if args.out is None:
        args.out = tempfile.mkdtemp(prefix="spectr-length-reaper-")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
