#!/usr/bin/env python3
"""Freeze Keys in a real host: REAPER renders Spectr Keys Dev offline and the
pitch of each played key is measured from the render.

One project per plugin format. Track 1 carries a 220 Hz sine through Spectr
Keys Dev with Freeze automated on at 2 s (the default Length, 1 bar at 120
BPM: a 2 s loop of the tone). Track 2 carries a MIDI item and sends MIDI only
(no audio) to track 1, the way a keyboard track drives an effect. Three keys
are played into the hold: 67 (a fifth up), 72 (an octave up) and 55 (a
fourth down). Each must sound at 220 Hz x 2^((note - 60) / 12).

The project is rendered OFFLINE (`REAPER -renderproject`; the session's audio
device is pointed at one that does not exist, so nothing reaches a speaker).

Zero pollution, by Pulp's daw-smoke ReaperSession: a fresh portable REAPER
resource directory and a temporary plugin scan folder, both removed on exit.
An AU is not copied anywhere: REAPER finds the component already installed
(the system registry), so the AU leg measures what the user has installed.

Usage:
  tools/reaper_freeze_keys.py --plugin build-keys/VST3/"Spectr Keys Dev.vst3"
  tools/reaper_freeze_keys.py --plugin build-keys/CLAP/"Spectr Keys Dev.clap"
  tools/reaper_freeze_keys.py --au      # the installed aumf SpKz
Exit: 0 every key within 5 cents; 1 a key off pitch, silent or a failed
render; 2 REAPER missing (SKIP -- not a pass).
"""
from __future__ import annotations

import argparse
import importlib.util
import math
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
import wave
from pathlib import Path

import numpy as np

SR = 48000
TONE_HZ = 220.0
FREEZE_AT = 2.0
SECONDS = 10.0
ROOT = 60
# (note, on, off) in seconds.
KEYS = ((67, 3.0, 4.5), (72, 5.0, 6.5), (55, 7.0, 8.5))
TOLERANCE_CENTS = 5.0
NAME = "Spectr Keys Dev"

BUILD_LUA = r'''
local out = os.getenv("SPECTR_KEYS_OUT")
local status = os.getenv("PULP_DAW_SMOKE_STATUS")
local fmt = os.getenv("SPECTR_KEYS_FMT")
local s = io.open(status, "w")
reaper.InsertTrackAtIndex(0, true)
reaper.InsertTrackAtIndex(1, true)
local fx_track = reaper.GetTrack(0, 0)
local midi_track = reaper.GetTrack(0, 1)
local fx = -1
for _, name in ipairs({fmt .. ": Spectr Keys Dev (Pulp)", fmt .. ":Spectr Keys Dev",
                       fmt .. ": Pulp: Spectr Keys Dev", "Spectr Keys Dev"}) do
  fx = reaper.TrackFX_AddByName(fx_track, name, false, -1)
  if fx >= 0 then break end
end
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
local _, fxname = reaper.TrackFX_GetFXName(fx_track, fx, "")
local freeze = -1
for i = 0, reaper.TrackFX_GetNumParams(fx_track, fx) - 1 do
  local _, name = reaper.TrackFX_GetParamName(fx_track, fx, i, "")
  if name == "Freeze" then freeze = i; break end
end
if freeze < 0 then s:write("FX_NOT_FOUND no Freeze parameter in " .. fxname); s:close(); return end
local env = reaper.GetFXEnvelope(fx_track, fx, freeze, true)
reaper.InsertEnvelopePoint(env, 0.0, 0.0, 1, 0, false, true)
reaper.InsertEnvelopePoint(env, tonumber(os.getenv("SPECTR_KEYS_FREEZE_AT")), 1.0, 1, 0, false, true)
reaper.Envelope_SortPoints(env)
-- The tone, on the effect's own track.
reaper.SetOnlyTrackSelected(fx_track)
reaper.SetEditCurPos(0.0, false, false)
reaper.InsertMedia(os.getenv("SPECTR_KEYS_WAV"), 0)
-- The keys, on their own track, sent to the effect as MIDI only.
local item = reaper.CreateNewMIDIItemInProj(midi_track, 0.0, tonumber(os.getenv("SPECTR_KEYS_SECONDS")), false)
local take = reaper.GetActiveTake(item)
for spec in string.gmatch(os.getenv("SPECTR_KEYS_NOTES"), "[^;]+") do
  local note, on, off = spec:match("([^,]+),([^,]+),([^,]+)")
  local p0 = reaper.MIDI_GetPPQPosFromProjTime(take, tonumber(on))
  local p1 = reaper.MIDI_GetPPQPosFromProjTime(take, tonumber(off))
  reaper.MIDI_InsertNote(take, false, false, p0, p1, 0, tonumber(note), 100, true)
end
reaper.MIDI_Sort(take)
local send = reaper.CreateTrackSend(midi_track, fx_track)
reaper.SetTrackSendInfo_Value(midi_track, 0, send, "I_SRCCHAN", -1)   -- no audio
reaper.SetTrackSendInfo_Value(midi_track, 0, send, "I_MIDIFLAGS", 0)  -- all channels
reaper.SetMediaTrackInfo_Value(midi_track, "B_MAINSEND", 0)
reaper.GetSetProjectInfo_String(0, "RENDER_FILE", out, true)
reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", "render", true)
reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
reaper.SetCurrentBPM(0, 120, false)
reaper.Main_SaveProjectEx(0, out .. "/keys.rpp", 0)
local dev_open, dev = reaper.GetAudioDeviceInfo("IDENT_OUT")
s:write("FX_SHOWN fx=" .. fxname .. " freeze=" .. freeze
        .. " device=" .. (dev_open and dev or "none"))
s:close()
'''

NO_DEVICE = "spectr-keys-no-device"


def log(msg):
    print(f"[reaper-keys] {msg}", flush=True)


def silence_audio_device(ini: Path) -> None:
    """Point CoreAudio in and out at a device that does not exist, so REAPER
    opens none (an offline render needs none) and nothing can play."""
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


def load_reaper_smoke(pulp_repo: Path):
    path = pulp_repo / "tools" / "testing" / "daw-smoke" / "reaper_smoke.py"
    spec = importlib.util.spec_from_file_location("reaper_smoke", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def write_tone(path: Path) -> None:
    t = np.arange(int(SECONDS * SR)) / SR
    x = 0.3 * np.sin(2 * np.pi * TONE_HZ * t)
    pcm = (np.stack([x, x], axis=1) * 32767).astype("<i2")
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


def tone_hz(y: np.ndarray, t0: float, t1: float) -> tuple[float, float]:
    """(frequency, rms) of the strongest tone in [t0, t1): a zero-padded,
    Hann-windowed spectrum peak refined by a parabola on log magnitude."""
    seg = y[int(t0 * SR):int(t1 * SR)]
    if len(seg) < 4096:
        return 0.0, 0.0
    rms = float(np.sqrt(np.mean(seg * seg)))
    n = 1 << 20
    spec = np.abs(np.fft.rfft(seg * np.hanning(len(seg)), n))
    lo = int(30.0 * n / SR)
    k = lo + int(np.argmax(spec[lo:]))
    a, b, c = (math.log(max(spec[k + d], 1e-30)) for d in (-1, 0, 1))
    frac = 0.5 * (a - c) / (a - 2 * b + c) if (a - 2 * b + c) != 0 else 0.0
    return (k + frac) * SR / n, rms


CACHE_READY = {
    # A finished VST3 scan names the plugin after the timestamp; an
    # interrupted one leaves only the timestamp.
    "vst3": (("reaper-vstplugins*.ini",), re.compile(r"(?m)^Spectr[ _]Keys[ _]Dev\.vst3=[^\n]*,[^\n]*,Spectr Keys Dev")),
    "clap": (("reaper-clap-*.ini",), re.compile(r"(?m)=\d+\|Spectr Keys Dev")),
    "au": (("reaper-auplugins*.ini",), re.compile(r"Spectr Keys Dev")),
}


def build_project(smoke, reaper: Path, ini: Path, fmt: str, env: dict, status: Path,
                  lua: Path, timeout: float) -> bool:
    """Scan once (a launch stopped as soon as REAPER's cache holds a finished
    scan of the plugin), then launch with the build script and wait for its
    handshake. Every launch is -newinst: a REAPER someone else has open is
    never handed this session's script."""
    globs, pattern = CACHE_READY[fmt]
    warm = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash"],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    deadline, found = time.time() + timeout, False
    try:
        while time.time() < deadline and not found:
            time.sleep(1.0)
            for g in globs:
                for cache in ini.parent.glob(g):
                    if pattern.search(cache.read_text(errors="replace")):
                        found = True
        time.sleep(1.0)
    finally:
        smoke.stop_reaper_process(warm)
    if not found:
        log("REAPER never finished scanning the plugin")
    proc = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash", str(lua)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, env=env)
    try:
        deadline = time.time() + timeout
        while time.time() < deadline:
            time.sleep(1.0)
            if status.exists():
                text = status.read_text()
                if text.startswith("FX_SHOWN"):
                    time.sleep(1.0)
                    return True
                if text.startswith("FX_NOT_FOUND"):
                    return False
        return False
    finally:
        smoke.stop_reaper_process(proc)


def render(reaper: Path, ini: Path, proj: Path, env: dict, timeout: float) -> None:
    """Render OFFLINE and quit; capped, and the REAPER started is stopped."""
    proc = subprocess.Popen([str(reaper), "-cfgfile", str(ini), "-newinst", "-nosplash",
                             "-ignoreerrors", "-renderproject", str(proj)],
                            env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        proc.wait(timeout=timeout)
    except subprocess.TimeoutExpired:
        log(f"render exceeded {timeout:.0f} s; stopping REAPER")
    finally:
        if proc.poll() is None:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)


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
    if args.au:
        fmt, plugin = "au", None
    else:
        plugin = Path(args.plugin).resolve()
        fmt = "clap" if plugin.suffix == ".clap" else "vst3"
    ns = argparse.Namespace(plugin_path=str(plugin) if plugin else "", plugin_name=NAME,
                            format=fmt, timeout=args.timeout)
    session = smoke.ReaperSession(reaper, ns)
    try:
        if fmt == "au":
            # Only the portable config: the component is found where it is
            # installed, and nothing is copied into a system folder.
            ns.format = "vst3"
            ns.plugin_path = str(out / "none.vst3")
            (out / "none.vst3").mkdir()
            if (code := session.place_plugin()) is not None:
                return code
            ns.format = "au"
        elif (code := session.place_plugin()) is not None:
            return code
        ini = session.portable / "reaper.ini"
        silence_audio_device(ini)
        status = session.portable / "status.txt"
        wav = out / "tone.wav"
        write_tone(wav)
        env = dict(os.environ, PULP_DAW_SMOKE_STATUS=str(status), SPECTR_KEYS_OUT=str(out),
                   SPECTR_KEYS_FMT={"au": "AU", "clap": "CLAP", "vst3": "VST3"}[fmt],
                   SPECTR_KEYS_WAV=str(wav), SPECTR_KEYS_SECONDS=str(SECONDS),
                   SPECTR_KEYS_FREEZE_AT=str(FREEZE_AT),
                   SPECTR_KEYS_NOTES=";".join(f"{n},{a},{b}" for n, a, b in KEYS))
        lua = session.portable / "build.lua"
        lua.write_text(BUILD_LUA)
        if not build_project(smoke, reaper, ini, fmt, env, status, lua, args.timeout):
            log("project build did not finish: "
                + (status.read_text() if status.exists() else "no status"))
            return 1
        built = status.read_text()
        log("built: " + built)
        if "device=none" not in built:
            log("REAPER opened an audio device; refusing to render -- FAIL")
            return 1
        render(reaper, ini, out / "keys.rpp", env, args.timeout)
        rendered = out / "render.wav"
        if not rendered.exists():
            log("no render -- FAIL")
            return 1
        y = read_wav(rendered)
        failures = 0
        held, held_rms = tone_hz(y, FREEZE_AT + 0.4, KEYS[0][1] - 0.05)
        print(f"\n{fmt.upper()} {built.split('fx=')[1].split(' freeze=')[0]}")
        print("| span | expected Hz | measured Hz | cents | rms | result |")
        print("|---|---:|---:|---:|---:|---|")
        cents = 1200 * math.log2(held / TONE_HZ) if held > 0 else float("nan")
        ok = abs(cents) <= TOLERANCE_CENTS and held_rms > 0.05
        failures += 0 if ok else 1
        print(f"| hold, no key | {TONE_HZ:.3f} | {held:.3f} | {cents:+.2f} | {held_rms:.3f} | "
              f"{'PASS' if ok else 'FAIL'} |")
        for note, on, off in KEYS:
            want = TONE_HZ * 2 ** ((note - ROOT) / 12)
            got, rms = tone_hz(y, on + 0.3, off - 0.05)
            cents = 1200 * math.log2(got / want) if got > 0 else float("nan")
            ok = abs(cents) <= TOLERANCE_CENTS and rms > 0.02
            failures += 0 if ok else 1
            print(f"| key {note} {on}-{off} s | {want:.3f} | {got:.3f} | {cents:+.2f} | "
                  f"{rms:.3f} | {'PASS' if ok else 'FAIL'} |")
        gap, gap_rms = tone_hz(y, KEYS[0][2] + 0.2, KEYS[1][1] - 0.02)
        ok = gap_rms < 0.01
        failures += 0 if ok else 1
        print(f"| no key held, between notes | silent | - | - | {gap_rms:.4f} | "
              f"{'PASS' if ok else 'FAIL'} |")
        log(f"{fmt.upper()}: {failures} failure(s)")
        return 1 if failures else 0
    finally:
        session.cleanup()
        if not args.keep:
            shutil.rmtree(out, ignore_errors=True)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--plugin", help="Spectr Keys Dev .vst3 or .clap to load")
    ap.add_argument("--au", action="store_true", help="use the installed aumf instead")
    ap.add_argument("--pulp-repo", default="~/Code/pulp", help="Pulp checkout (daw-smoke harness)")
    ap.add_argument("--out", default=None, help="work directory (default: a new temp dir)")
    ap.add_argument("--timeout", type=int, default=120, help="cap, in seconds, on each REAPER launch")
    ap.add_argument("--keep", action="store_true", help="keep the project and render")
    args = ap.parse_args()
    if not args.au and not args.plugin:
        ap.error("--plugin or --au")
    if args.out is None:
        args.out = tempfile.mkdtemp(prefix="spectr-keys-reaper-")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
