#!/usr/bin/env python3
"""Production Spectr host automation, audio and cold-reopen qualification.

Opt-in macOS VST3/CLAP only; no install and no editor interaction. Uses normal
REAPER parameter/envelope APIs and an in-process synchronous offline render
from a temporary copied bundle, not the installed plugin path.
Every rendering host is hashed before and after rendering, then stopped. The
provided executable digest must come from the exact release artifact being
qualified. This does not establish package provenance or visible widget behavior.
No plugin fixture/environment seam is used. Results are retained on failure.
"""
from __future__ import annotations

import argparse
import array
import json
import math
import os
import re
import struct
import subprocess
import sys
import time
import wave
from pathlib import Path

from reaper_loaded_binary import bundle_executable, sha256, verify_loaded_binary

SR = 48000
SECONDS = 9
REQUIRED = {"Output": 0.5, "Mix": 0.0, "Intensity": 0.37,
            "Auto Gain": 0.0, "LFO Enabled": 0.0}
WINDOWS = ((1.0, 2.0, 0.0), (4.0, 5.0, -12.0), (7.0, 8.0, 0.0))

LUA = r'''
local dir = os.getenv("REAPER_PRODUCTION_QUAL_DIR")
local phase = os.getenv("REAPER_PRODUCTION_QUAL_PHASE")
local function status(value)
  local f = assert(io.open(dir .. "/" .. phase .. ".status", "w"))
  f:write(value); f:close()
end
local function main()
  local tr, fx
  if phase == "author" then
    assert(reaper.CountTracks(0) == 0, "author project is not empty")
    assert(reaper.TrackFX_GetCount(reaper.GetMasterTrack(0)) == 0, "unexpected master FX")
    reaper.InsertTrackAtIndex(0, true)
    tr = reaper.GetTrack(0, 0)
    fx = reaper.TrackFX_AddByName(tr, os.getenv("REAPER_PRODUCTION_QUAL_FX"), false, -1)
    assert(fx == 0 and reaper.TrackFX_GetCount(tr) == 1, "qualified-format Spectr missing/ambiguous")
    local item = reaper.AddMediaItemToTrack(tr)
    local take = reaper.AddTakeToMediaItem(item)
    local src = reaper.PCM_Source_CreateFromFile(dir .. "/input.wav")
    assert(src, "cannot load deterministic input")
    reaper.SetMediaItemTake_Source(take, src)
    reaper.SetMediaItemInfo_Value(item, "D_POSITION", 0)
    reaper.SetMediaItemInfo_Value(item, "D_LENGTH", 9)
    reaper.SetMediaTrackInfo_Value(tr, "D_VOL", 1)
    reaper.SetMediaTrackInfo_Value(tr, "D_PAN", 0)
    reaper.SetMediaTrackInfo_Value(tr, "I_AUTOMODE", 1)
    local master = reaper.GetMasterTrack(0)
    reaper.SetMediaTrackInfo_Value(master, "D_VOL", 1)
    reaper.SetMediaTrackInfo_Value(master, "B_MUTE", 0)
  else
    assert(reaper.CountTracks(0) == 1, "reopened track missing/ambiguous")
    tr, fx = reaper.GetTrack(0, 0), 0
    assert(reaper.TrackFX_GetCount(tr) == 1, "reopened FX missing/ambiguous")
  end
  local _, fxname = reaper.TrackFX_GetFXName(tr, fx, "")
  assert(fxname:find("Spectr", 1, true), "wrong FX name")
  local prefix = os.getenv("REAPER_PRODUCTION_QUAL_FX"):match("^([^:]+:)")
  assert(fxname:sub(1, #prefix) == prefix, "wrong FX format")
  local idx = {}
  for i = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do
    local _, name = reaper.TrackFX_GetParamName(tr, fx, i, "")
    assert(idx[name] == nil, "ambiguous parameter: " .. name)
    idx[name] = i
  end
  local required = { ["Output"]=0.5, ["Mix"]=0, ["Intensity"]=0.37,
                     ["Auto Gain"]=0, ["LFO Enabled"]=0 }
  for name, value in pairs(required) do
    assert(idx[name] ~= nil, "missing parameter: " .. name)
    if phase == "author" then reaper.TrackFX_SetParamNormalized(tr, fx, idx[name], value) end
  end
  if phase == "author" then
    local e = assert(reaper.GetFXEnvelope(tr, fx, idx["Output"], true))
    local _, lo, hi = reaper.TrackFX_GetParam(tr, fx, idx["Output"])
    assert(hi > lo, "invalid Output host range")
    local scaling = reaper.GetEnvelopeScalingMode(e)
    for _, point in ipairs({{0,0.5},{3,0.25},{6,0.5}}) do
      local raw = lo + (hi - lo) * point[2]
      reaper.InsertEnvelopePoint(e, point[1], reaper.ScaleToEnvelopeMode(scaling, raw), 1, 0, false, true)
    end
    reaper.Envelope_SortPoints(e)
    local ok, chunk = reaper.GetEnvelopeStateChunk(e, "", false)
    assert(ok, "missing Output envelope chunk")
    chunk = chunk:gsub("\nACT %d[^\n]*", "\nACT 1 -1")
    assert(reaper.SetEnvelopeStateChunk(e, chunk, false), "cannot activate Output lane")
  end
  reaper.SetGlobalAutomationOverride(-1)
  reaper.SetEditCurPos(0, false, false)
  reaper.GetSetProjectInfo(0, "RENDER_NORMALIZE", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_DITHER", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_FADEIN", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_FADEOUT", 0, true)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "PROJECT_SRATE_USE", 1, true)
  reaper.GetSetProjectInfo(0, "RENDER_SRATE", 48000, true)
  reaper.GetSetProjectInfo(0, "RENDER_CHANNELS", 2, true)
  reaper.GetSetProjectInfo(0, "RENDER_BOUNDSFLAG", 1, true)
  reaper.GetSetProjectInfo(0, "RENDER_SETTINGS", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_TAILFLAG", 0, true)
  reaper.GetSetProjectInfo(0, "RENDER_ADDTOPROJ", 0, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_FILE", dir, true)
  reaper.GetSetProjectInfo_String(0, "RENDER_PATTERN", phase, true)
  -- Ordinary WAV/PCM24 config (fourcc 'evaw', bit depth 24).
  reaper.GetSetProjectInfo_String(0, "RENDER_FORMAT", "ZXZhdxgAAA==", true)
  local function facts(suffix)
    local f = assert(io.open(dir .. "/" .. phase .. "-" .. suffix .. ".tsv", "w"))
    for name, _ in pairs(required) do
      local i = idx[name]
      local _, lo, hi = reaper.TrackFX_GetParam(tr, fx, i)
      f:write(string.format("PARAM\t%d\t%s\t%.17g\t%.17g\t%.17g\n", i, name, lo, hi,
                           reaper.TrackFX_GetParamNormalized(tr, fx, i)))
    end
    local e = assert(reaper.GetFXEnvelope(tr, fx, idx["Output"], false), "missing saved Output envelope")
    local scaling = reaper.GetEnvelopeScalingMode(e)
    local _, lo, hi = reaper.TrackFX_GetParam(tr, fx, idx["Output"])
    for i = 0, reaper.CountEnvelopePoints(e) - 1 do
      local ok, t, v, shape = reaper.GetEnvelopePoint(e, i)
      assert(ok, "cannot read envelope point")
      v = reaper.ScaleFromEnvelopeMode(scaling, v)
      f:write(string.format("POINT\t%.17g\t%.17g\t%d\n", t, (v-lo)/(hi-lo), shape))
    end
    local dev = reaper.GetAudioDeviceInfo("IDENT_OUT")
    f:write("DEVICE\t" .. (dev and "open" or "none") .. "\n")
    f:close()
  end
  facts("before")
  status("READY")
  local function render_when_permitted()
    local f = io.open(dir .. "/" .. phase .. ".go", "r")
    if not f then reaper.defer(render_when_permitted); return end
    f:close()
    -- Python has verified this live host's mapping before issuing .go.
    local ok, err = xpcall(function()
      reaper.Main_OnCommand(42230, 0) -- render with current settings, auto-close
      reaper.SetEditCurPos(0, false, false)
      facts("after")
      reaper.Main_SaveProjectEx(0, dir .. "/" .. phase .. ".rpp", 0)
      status("DONE")
    end, debug.traceback)
    if not ok then status("ERROR " .. err) end
  end
  reaper.defer(render_when_permitted)
end
local ok, err = xpcall(main, debug.traceback)
if not ok then status("ERROR " .. err) end
'''


def write_input(path: Path) -> None:
    """Stationary stereo two-tone, low enough to avoid clipping at unity."""
    pcm = array.array("h")
    for i in range(SR * SECONDS):
        value = round(32767 * (0.12 * math.sin(2 * math.pi * 431 * i / SR)
                              + 0.08 * math.sin(2 * math.pi * 997 * i / SR)))
        pcm.extend((value, value))
    if sys.byteorder != "little":
        pcm.byteswap()
    with wave.open(str(path), "wb") as stream:
        stream.setnchannels(2)
        stream.setsampwidth(2)
        stream.setframerate(SR)
        stream.writeframes(pcm.tobytes())


def read_audio(path: Path) -> tuple[int, list[list[float]]]:
    """Decode PCM WAV or IEEE float WAV, including extensible subtype headers."""
    data = path.read_bytes()
    if data[:4] != b"RIFF" or data[8:12] != b"WAVE":
        raise ValueError("render is not a RIFF WAV")
    pos, fmt, raw = 12, None, None
    while pos + 8 <= len(data):
        tag, size = struct.unpack_from("<4sI", data, pos)
        payload = data[pos + 8:pos + 8 + size]
        if len(payload) != size:
            raise ValueError("truncated WAV chunk")
        if tag == b"fmt ":
            fmt = payload
        elif tag == b"data":
            raw = payload
        pos += 8 + size + (size % 2)
    if fmt is None or len(fmt) < 16 or raw is None:
        raise ValueError("missing WAV format/data")
    kind, channels, rate, _, align, bits = struct.unpack_from("<HHIIHH", fmt)
    if kind == 0xFFFE and len(fmt) >= 40:
        kind = struct.unpack_from("<H", fmt, 24)[0]
    if channels != 2 or bits not in (16, 24, 32) or align != channels * bits // 8:
        raise ValueError("unexpected WAV channels/sample format")
    if len(raw) % align or kind not in (1, 3) or (kind == 3 and bits != 32):
        raise ValueError("invalid WAV data encoding")
    width = bits // 8
    samples = []
    for at in range(0, len(raw), width):
        sample = (struct.unpack_from("<f", raw, at)[0] if kind == 3 else
                  int.from_bytes(raw[at:at + width], "little", signed=True) / (1 << (bits - 1)))
        if not math.isfinite(sample):
            raise ValueError("nonfinite rendered audio")
        samples.append(sample)
    return rate, [samples[0::2], samples[1::2]]


def analyse_audio(source, rendered) -> dict:
    rate, reference = source
    render_rate, channels = rendered
    if rate != SR or render_rate != SR or any(len(x) != SR * SECONDS for x in channels):
        raise ValueError("wrong render sample rate or duration")
    if max(abs(v) for channel in channels for v in channel) >= 0.95:
        raise ValueError("clipped or unexpectedly loud output")
    gains = []
    for start, end, wanted in WINDOWS:
        a, b = round(start * SR), round(end * SR)
        for index, channel in enumerate(channels):
            input_power = sum(v * v for v in reference[index][a:b]) / (b - a)
            output_power = sum(v * v for v in channel[a:b]) / (b - a)
            if input_power < 1e-6 or output_power < 1e-10:
                raise ValueError("silent input/output cannot prove automation")
            gain = 10 * math.log10(output_power / input_power)
            if abs(gain - wanted) > 0.25:
                raise ValueError(f"Output automation gain {gain:.3f} dB, expected {wanted} dB")
            # A wrong signal with the right RMS is not an audio pass. Preserve
            # both probe tones, allowing phase shifts from latency compensation.
            amplitudes = []
            for frequency in (431, 997):
                basis = [(math.sin(2 * math.pi * frequency * i / SR),
                          math.cos(2 * math.pi * frequency * i / SR)) for i in range(b - a)]
                def amplitude(values):
                    sine = sum(v * trig[0] for v, trig in zip(values, basis))
                    cosine = sum(v * trig[1] for v, trig in zip(values, basis))
                    return 2 * math.hypot(sine, cosine) / (b - a)
                input_amp = amplitude(reference[index][a:b])
                output_amp = amplitude(channel[a:b])
                if input_amp <= 0 or output_amp <= 0:
                    raise ValueError("missing input/output probe tone")
                tone_gain = 20 * math.log10(output_amp / input_amp)
                if abs(tone_gain - wanted) > 0.25:
                    raise ValueError("probe tone gain does not follow Output automation")
                amplitudes.append(output_amp)
            residual = max(0, output_power - sum(v * v / 2 for v in amplitudes)) / output_power
            if residual > 0.001:
                raise ValueError("unexpected non-probe energy in dry output")
            gains.append({"start": start, "end": end, "channel": index, "gain_db": gain,
                          "non_probe_power_fraction": residual})
    return {"window_gains": gains, "tolerance_db": 0.25}


def compare_reopened(first, reopened) -> dict:
    if first[0] != reopened[0] or any(len(a) != len(b) for a, b in zip(first[1], reopened[1])):
        raise ValueError("reopened audio dimensions changed")
    error = max(abs(a - b) for x, y in zip(first[1], reopened[1]) for a, b in zip(x, y))
    if error > 0.0003:
        raise ValueError(f"reopened audio differs: peak sample error {error}")
    return {"peak_sample_error": error, "tolerance": 0.0003}


def parse_host_facts(text: str, check_values: bool = True) -> dict:
    params, points, devices = {}, [], []
    indices = set()
    for line in text.splitlines():
        fields = line.split("\t")
        if fields[0] == "PARAM" and len(fields) == 6:
            _, index, name, lo, hi, value = fields
            index, lo, hi, value = int(index), float(lo), float(hi), float(value)
            if name in params or index in indices or name not in REQUIRED or index < 0:
                raise ValueError("ambiguous/unexpected host parameter")
            if not all(math.isfinite(v) for v in (lo, hi, value)) or hi <= lo:
                raise ValueError("invalid host parameter range/value")
            if check_values and abs(value - REQUIRED[name]) > 0.001:
                raise ValueError(f"host parameter {name} was not restored/set correctly")
            params[name] = {"index": index, "min": lo, "max": hi, "normalized": value}
            indices.add(index)
        elif fields[0] == "POINT" and len(fields) == 4:
            points.append((float(fields[1]), float(fields[2]), int(fields[3])))
        elif fields[0] == "DEVICE" and len(fields) == 2:
            devices.append(fields[1])
        else:
            raise ValueError("malformed host facts")
    if set(params) != set(REQUIRED) or devices != ["none"]:
        raise ValueError("missing parameters or audio device was opened")
    wanted = ((0, 0.5, 1), (3, 0.25, 1), (6, 0.5, 1))
    if len(points) != 3 or any(not math.isfinite(v) for p in points for v in p):
        raise ValueError("missing/invalid automation points")
    if any(abs(t - wt) > 1e-6 or abs(v - wv) > 1e-6 or s != ws
           for (t, v, s), (wt, wv, ws) in zip(points, wanted)):
        raise ValueError("wrong Output automation points")
    return {"params": params, "points": points, "device": "none"}


def require_receipt_pair(before: dict, after: dict, expected: str) -> None:
    for receipt in (before, after):
        if (receipt.get("status") != "PASS" or receipt.get("expected_sha256") != expected
                or receipt.get("actual_sha256") != expected or not receipt.get("actual_path")
                or not isinstance(receipt.get("pid"), int) or receipt["pid"] <= 0):
            raise ValueError("missing or mismatched rendering-host binary receipt")
    if before["pid"] != after["pid"] or before["actual_path"] != after["actual_path"]:
        raise ValueError("rendering host identity changed")


def wait_status(proc, path: Path, wanted: str, timeout: float) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if proc.poll() is not None:
            raise RuntimeError("owned REAPER exited before " + wanted)
        if path.exists():
            value = path.read_text()
            if value.startswith("ERROR"):
                raise RuntimeError(value)
            if value == wanted:
                return
        time.sleep(0.1)
    raise TimeoutError("REAPER did not reach " + wanted)


def host_environment(environment: dict) -> dict:
    """Never inherit a plugin test seam from an earlier development session."""
    return {key: value for key, value in environment.items() if not key.startswith("SPECTR_")}


def host_command(reaper: Path, script: Path, ini: Path, project: Path | None) -> list[str]:
    # Put Lua before -cfgfile following an observed Windows REAPER launch fix;
    # this is a conservative order, not a proven exclusive macOS requirement.
    return [str(reaper), str(script), "-cfgfile", str(ini), "-newinst", "-nosplash",
            "-ignoreerrors"] + ([str(project)] if project else [])


def run(args) -> int:
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)  # never remove or reuse existing evidence
    result = {"status": "FAIL", "scope": "temporary copied VST3/CLAP bundle; host Output automation and cold reopen",
              "installed_path_verified": False,
              "gui_verified": False, "package_provenance_verified": False,
              "separate_render_processes_used": False, "expected_sha256": args.expected_sha256}
    session = None
    try:
        if sys.platform != "darwin":
            raise RuntimeError("qualification requires macOS")
        plugin = args.plugin.resolve(strict=True)
        if plugin.suffix not in (".clap", ".vst3"):
            raise ValueError("only VST3/CLAP supported; AU requires a separate installed-consumer lane")
        if not re.fullmatch(r"[0-9a-f]{64}", args.expected_sha256):
            raise ValueError("expected SHA256 must be 64 lowercase hexadecimal characters")
        if sha256(bundle_executable(plugin)) != args.expected_sha256:
            raise ValueError("provided executable does not match exact-release SHA256")
        checker = Path(__file__).with_name("check_no_test_seams.py")
        checked = subprocess.run([sys.executable, "-B", str(checker), "--expect", "absent",
                                  str(plugin)], capture_output=True, text=True, timeout=60)
        (out / "test-seams-check.txt").write_text(checked.stdout + checked.stderr)
        if checked.returncode:
            raise ValueError("production test-seam absence check failed")
        from reaper_freeze_length import load_reaper_smoke, silence_audio_device, warm_scan
        smoke = load_reaper_smoke(args.pulp_repo.expanduser().resolve())
        reaper = args.reaper.resolve(strict=True)
        result["host_executable"] = {"path": str(reaper), "sha256": sha256(reaper)}
        fmt = "clap" if plugin.suffix == ".clap" else "vst3"
        session = smoke.ReaperSession(reaper, argparse.Namespace(
            plugin_path=str(plugin), plugin_name="Spectr", format=fmt, timeout=args.timeout))
        if session.place_plugin() is not None:
            raise RuntimeError("isolated plugin staging failed")
        ini = session.portable / "reaper.ini"
        silence_audio_device(ini)
        base_env = host_environment(os.environ)
        if not warm_scan(reaper, ini, fmt, args.timeout, env=base_env):
            raise RuntimeError("isolated plugin scan did not complete")
        write_input(out / "input.wav")
        script = out / "qualification.lua"
        script.write_text(LUA)
        base_env.update(REAPER_PRODUCTION_QUAL_DIR=str(out),
                        REAPER_PRODUCTION_QUAL_FX=("CLAP: " if fmt == "clap" else "VST3: ") + "Spectr")
        result["stages"] = {}
        source = read_audio(out / "input.wav")
        renders = []
        pids = []
        for phase in ("author", "reopen"):
            project = None if phase == "author" else out / "author.rpp"
            env = dict(base_env, REAPER_PRODUCTION_QUAL_PHASE=phase)
            with (out / f"{phase}-host.log").open("w") as log:
                proc = subprocess.Popen(host_command(reaper, script, ini, project),
                                        env=env, stdout=log, stderr=subprocess.STDOUT)
            session.proc = proc  # canonical ownership/teardown; no global process operations
            try:
                status = out / f"{phase}.status"
                wait_status(proc, status, "READY", args.timeout)
                before_facts = parse_host_facts((out / f"{phase}-before.tsv").read_text(),
                                                check_values=phase == "reopen")
                before = verify_loaded_binary(proc, plugin, out / f"{phase}-binary-before.json",
                                              phase + " same-host synchronous render: before")
                (out / f"{phase}.go").write_text("render")
                wait_status(proc, status, "DONE", args.timeout)
                after = verify_loaded_binary(proc, plugin, out / f"{phase}-binary-after.json",
                                             phase + " same-host synchronous render: after")
                require_receipt_pair(before, after, args.expected_sha256)
                after_facts = parse_host_facts((out / f"{phase}-after.tsv").read_text())
                if not (out / f"{phase}.rpp").is_file():
                    raise ValueError("host did not save its project")
                audio = read_audio(out / f"{phase}.wav")
                facts = analyse_audio(source, audio)
                result["stages"][phase] = {"before": before_facts, "after": after_facts,
                                           "audio": facts, "binary_before": before, "binary_after": after,
                                           "wav_sha256": sha256(out / f"{phase}.wav"),
                                           "project_sha256": sha256(out / f"{phase}.rpp")}
                renders.append(audio)
                pids.append(proc.pid)
            finally:
                session.terminate()
        if pids[0] == pids[1]:
            raise ValueError("cold-reopen process identity was reused")
        result["cold_reopen_audio"] = compare_reopened(*renders)
        if sha256(bundle_executable(plugin)) != args.expected_sha256:
            raise ValueError("intended release executable changed during qualification")
        if sha256(reaper) != result["host_executable"]["sha256"]:
            raise ValueError("REAPER executable changed during qualification")
        result["status"] = "PASS"
    except Exception as exc:
        result["error"] = str(exc)
    finally:
        if session is not None:
            session.cleanup()
        (out / "result.json").write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps({"status": result["status"], "error": result.get("error")}), flush=True)
    return 0 if result["status"] == "PASS" else 1


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--plugin", type=Path, required=True)
    ap.add_argument("--expected-sha256", required=True, help="executable digest from exact release artifact")
    ap.add_argument("--reaper", type=Path, required=True, help="exact REAPER executable")
    ap.add_argument("--pulp-repo", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True, help="new evidence directory; always retained")
    ap.add_argument("--timeout", type=float, default=120)
    args = ap.parse_args()
    if not math.isfinite(args.timeout) or not 1 <= args.timeout <= 600:
        ap.error("--timeout must be between 1 and 600 seconds")
    return run(args)


if __name__ == "__main__":
    sys.exit(main())
