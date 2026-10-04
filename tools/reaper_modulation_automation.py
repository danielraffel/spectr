#!/usr/bin/env python3
"""Automation and modulation of Spectr's header controls in a real REAPER.

One scripted REAPER session per format (VST3, CLAP, AU) plays three passes
over the INTENSITY knob, with the editor open:

  record    automation mode Touch. The editor drives one real knob gesture
            (begin -> a ramp of values -> end, the bracket a drag sends) a
            few seconds after it opens. REAPER must record it as Intensity
            envelope points.
  playback  automation mode Read. Nobody touches anything: the editor's
            INTENSITY knob must follow the recorded lane (its displayed base
            value walks the same ramp).
  modulate  LFO 1 on, routed to Intensity at Depth 100 % (Bank off),
            automation mode Touch. The knob's base must still follow the lane,
            its modulated marker must move around it, and REAPER must record
            NOTHING: modulation is display-only and never writes a host lane
            (the Intensity envelope's point count is unchanged, and no other
            envelope appears).

The editor side is observed through a fixture the editor runs at open
(SPECTR_EVAL): it samples the knob's displayed base (aria-valuenow) and the
modulated value it draws every 100 ms and prints them as `[ui-sample]` lines,
and it performs the one gesture through the same path a drag takes
(spectrParamGesture + param_edit). The host side is observed by the Lua
script: envelope point counts and values after each pass, and the parameter
value REAPER reads during playback.

Silent: the master is muted at -inf, and the session's audio device is
REAPER's own choice from the copied settings (playback needs one). Zero
pollution via Pulp's daw-smoke ReaperSession (portable resource dir, temp
scan path, transient AU install removed on exit). AU needs a GUI login
session: run it on the machine (or through `launchctl asuser`), not over
plain SSH.

Usage:
  tools/reaper_modulation_automation.py --plugin "build/VST3/Spectr ModUi Dev.vst3" \
      --name "Spectr ModUi Dev" --pulp-repo ~/Code/pulp --out DIR
Exit: 0 every check passed, 1 a check failed, 2 REAPER missing (SKIP).
"""
from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shutil
import sys
import time
from pathlib import Path

PASS_SECONDS = 9.0
# Inside the record pass (the second): the control pass is the first 9 s.
GESTURE_DELAY_MS = 14000

LUA = r'''
local out = os.getenv("SPECTR_AUTO_OUT")
local status = os.getenv("PULP_DAW_SMOKE_STATUS")
local want = os.getenv("PULP_DAW_SMOKE_FX")
local pass_seconds = tonumber(os.getenv("SPECTR_AUTO_PASS_SECONDS"))
local logf = io.open(out .. "/host.log", "w")
local function log(s) logf:write(string.format("%.3f ", reaper.time_precise()) .. s .. "\n"); logf:flush() end

reaper.InsertTrackAtIndex(0, true)
local tr = reaper.GetTrack(0, 0)
local fx = -1
local spaced = (want:gsub(":", ": "))
for _, name in ipairs({want, spaced .. " (Pulp)", spaced}) do
  fx = reaper.TrackFX_AddByName(tr, name, false, -1)
  if fx >= 0 then break end
end
if fx < 0 then
  local s = io.open(status, "w"); s:write("FX_NOT_FOUND " .. want); s:close(); return
end
local idx = {}
for i = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do
  local _, name = reaper.TrackFX_GetParamName(tr, fx, i, "")
  idx[name] = i
end
for _, n in ipairs({"Intensity", "LFO Enabled", "LFO Rate", "LFO 1 Bank", "LFO 1 Intensity",
                    "LFO 1 Intensity Depth", "Output"}) do
  if idx[n] == nil then
    local s = io.open(status, "w"); s:write("FX_NOT_FOUND param " .. n); s:close(); return
  end
end
local master = reaper.GetMasterTrack(0)
reaper.SetMediaTrackInfo_Value(master, "B_MUTE", 1)
reaper.SetMediaTrackInfo_Value(master, "D_VOL", 0)
reaper.SetMediaTrackInfo_Value(tr, "I_AUTOMODE", 1)  -- read while we set up
reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO Enabled"], 0)
reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO 1 Bank"], 0)
reaper.TrackFX_SetParamNormalized(tr, fx, idx["Intensity"], 1)
local ienv = reaper.GetFXEnvelope(tr, fx, idx["Intensity"], true)
-- A lane created from a script starts inactive and unarmed: active, so Read
-- plays it, and armed, so Touch writes to it.
do
  local ok, chunk = reaper.GetEnvelopeStateChunk(ienv, "", false)
  chunk = chunk:gsub("\nACT %d[^\n]*", "\nACT 1 -1"):gsub("\nARM %d", "\nARM 1")
  if not chunk:find("\nARM ") then chunk = chunk:gsub("\nACT ", "\nARM 1\nACT ", 1) end
  reaper.SetEnvelopeStateChunk(ienv, chunk, false)
  local _, after = reaper.GetEnvelopeStateChunk(ienv, "", false)
  local f = io.open(out .. "/envelope-chunk.txt", "w"); f:write(after); f:close()
end
reaper.TrackFX_Show(tr, fx, 3)
local function envelopes()
  local n = 0
  for i = 0, reaper.TrackFX_GetNumParams(tr, fx) - 1 do
    local e = reaper.GetFXEnvelope(tr, fx, i, false)
    if e and reaper.CountEnvelopePoints(e) > 0 then n = n + 1 end
  end
  return n
end
local function points()
  local n = reaper.CountEnvelopePoints(ienv)
  local lo, hi = 1e9, -1e9
  for i = 0, n - 1 do
    local _, _, v = reaper.GetEnvelopePoint(ienv, i)
    lo = math.min(lo, v); hi = math.max(hi, v)
  end
  return n, lo, hi
end
local s = io.open(status, "w"); s:write("FX_SHOWN"); s:close()
local dev_open, dev = reaper.GetAudioDeviceInfo("IDENT_OUT")
log("fx " .. fx .. " intensity=" .. idx["Intensity"] .. " device=" .. (dev_open and dev or "none"))

local phases = {"control", "record", "playback", "modulate"}
local phase, t0, stopping, host_wrote = 0, 0, nil, false
local function start(p)
  phase = p
  local name = phases[p]
  reaper.OnStopButton()
  reaper.SetEditCurPos(0, false, false)
  -- Output marks the pass in the editor's samples (0, -1, -2 dB), set in Read.
  reaper.SetMediaTrackInfo_Value(tr, "I_AUTOMODE", 1)
  reaper.TrackFX_SetParamNormalized(tr, fx, idx["Output"], (24 - (p - 1)) / 48)
  if name == "control" then
    -- Positive control: a lane the script writes must play back, or this
    -- session cannot judge anything recorded.
    reaper.InsertEnvelopePoint(ienv, 4.0, 0.2, 0, 0, false, true)
    reaper.Envelope_SortPoints(ienv)
  end
  if name == "record" then
    reaper.DeleteEnvelopePointRange(ienv, 0.5, 1000)
  end
  if name == "modulate" then
    reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO Rate"], (1 - 0.25) / (16 - 0.25))
    reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO 1 Intensity Depth"], 1)
    reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO 1 Intensity"], 1)
    reaper.TrackFX_SetParamNormalized(tr, fx, idx["LFO Enabled"], 1)
  end
  local n, lo, hi = points()
  log("begin " .. name .. " points=" .. n .. " envelopes=" .. envelopes())
  local write_mode = tonumber(os.getenv("SPECTR_AUTO_WRITE_MODE") or "2")
  reaper.SetMediaTrackInfo_Value(tr, "I_AUTOMODE", (name == "playback" or name == "control") and 1 or write_mode)
  log("automode=" .. reaper.GetTrackAutomationMode(tr) .. " global=" .. reaper.GetGlobalAutomationOverride())
  reaper.OnPlayButton()
  t0 = reaper.time_precise()
end
local function finish()
  -- Read after the stop has settled: REAPER commits a written pass on stop.
  reaper.SetMediaTrackInfo_Value(tr, "I_AUTOMODE", 1)
  local n, lo, hi = points()
  log(string.format("end %s points=%d lo=%.4f hi=%.4f envelopes=%d", phases[phase], n, lo, hi, envelopes()))
end
local function tick()
  local now = reaper.time_precise()
  if phase == 0 then
    if now - t0 > 0 then t0 = now; phase = -1 end
  end
  if phase == -1 then
    if now - t0 > 1.0 then start(1) end
  elseif phase >= 1 then
    if reaper.GetPlayState() & 1 == 1 then
      local touched, ttr, tfx, tparam = reaper.GetLastTouchedFX()
      log(string.format("%s pos=%.3f param=%.4f touched=%s:%s", phases[phase], reaper.GetPlayPosition(),
                        reaper.TrackFX_GetParamNormalized(tr, fx, idx["Intensity"]),
                        tostring(touched), tostring(tparam)))
    end
    if os.getenv("SPECTR_AUTO_HOST_WRITE") == "1" and phases[phase] == "record"
        and now - t0 > 3 and not host_wrote then
      host_wrote = true
      reaper.TrackFX_SetParamNormalized(tr, fx, idx["Intensity"], 0.33)
      log("host write 0.33")
    end
    if now - t0 > pass_seconds and not stopping then
      reaper.OnStopButton()
      stopping = now
    end
    if stopping and now - stopping > 1.5 then
      stopping = nil
      finish()
      if phase < #phases then start(phase + 1)
      else
        log("done")
        logf:close()
        local d = io.open(out .. "/done.txt", "w"); d:write("DONE"); d:close()
        return
      end
    end
  end
  reaper.defer(tick)
end
t0 = reaper.time_precise()
reaper.defer(tick)
'''

# The editor fixture. Sampling every 100 ms from editor open; one gesture,
# GESTURE_DELAY_MS after open, through the knob's own write path.
EDITOR_JS = r'''
(() => {
  const t0 = Date.now();
  const knob = (name) => document.querySelector('[data-spectr-knob="' + name + '"]');
  const num = (el) => el ? Number(el.getAttribute("aria-valuenow")) : NaN;
  setInterval(() => {
    const m = globalThis.__spectrModControls;
    const s = m ? m.state : null;
    const base = num(knob("intensity"));
    console.log("[ui-sample] " + JSON.stringify({
      t: Date.now() - t0, intensity: base, output: num(knob("output-trim")),
      modOn: !!(s && s.intensityOn),
      played: s && s.intensityOn ? +(base * (1 - s.intensityPull)).toFixed(2) : null,
      drawn: m && m.drawn ? (m.drawn.intensity || "").length : 0 }));
  }, 100);
  setTimeout(() => {
    const ramp = [];
    for (let i = 0; i <= 20; i++) ramp.push(100 - 80 * i / 20);
    for (let i = 1; i <= 10; i++) ramp.push(20 + 40 * i / 10);
    console.log("[ui-gesture] begin");
    globalThis.spectrParamGesture && globalThis.spectrParamGesture(5000, true);
    let k = 0;
    const step = setInterval(() => {
      window.pulp.postMessage("param_edit", { id: 5000, value: ramp[k] }, "spectr-level-intensity");
      k++;
      if (k >= ramp.length) {
        clearInterval(step);
        globalThis.spectrParamGesture && globalThis.spectrParamGesture(5000, false);
        console.log("[ui-gesture] end");
      }
    }, 50);
  }, %GESTURE_DELAY_MS%);
})();
'''


def log(msg):
    print(f"[reaper-modauto] {msg}", flush=True)


def load_reaper_smoke(pulp_repo: Path):
    path = pulp_repo / "tools" / "testing" / "daw-smoke" / "reaper_smoke.py"
    spec = importlib.util.spec_from_file_location("reaper_smoke", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def analyse(host_log: str, ui_log: str) -> tuple[list[str], dict]:
    """Every check, as (failures, facts)."""
    failures: list[str] = []
    facts: dict = {}
    ends = {m.group(1): (int(m.group(2)), float(m.group(3)), float(m.group(4)), int(m.group(5)))
            for m in re.finditer(r"end (\w+) points=(\d+) lo=([-0-9.e]+) hi=([-0-9.e]+) envelopes=(\d+)",
                                 host_log)}
    facts["envelope_after"] = {k: {"points": v[0], "lo": v[1], "hi": v[2], "envelopes": v[3]}
                               for k, v in ends.items()}
    if set(ends) != {"control", "record", "playback", "modulate"}:
        failures.append(f"passes did not all finish: {sorted(ends)}")
        return failures, facts
    rec, play, mod = ends["record"], ends["playback"], ends["modulate"]
    control_vals = [float(m.group(1)) for m in re.finditer(r"control pos=[0-9.]+ param=([0-9.]+)", host_log)]
    facts["control_param_range"] = [min(control_vals), max(control_vals)] if control_vals else None
    if not control_vals or min(control_vals) > 0.3:
        failures.append("positive control: a scripted lane (1.0 -> 0.2) did not play back; "
                        "this session cannot judge recording")
    # record: the gesture became automation (Intensity 100 -> 20 -> 60 %).
    if rec[0] < 4:
        failures.append(f"record: only {rec[0]} Intensity envelope points written")
    # VST3 and AU lanes are normalised 0..1; a CLAP lane is the parameter's
    # plain range (Intensity 0..100 %).
    scale = 100.0 if rec[2] > 1.5 else 1.0
    if not (rec[1] / scale <= 0.3 and rec[2] / scale >= 0.9):
        failures.append(f"record: envelope range {rec[1]:.3f}..{rec[2]:.3f} does not hold the ramp")
    # modulate: modulation wrote nothing.
    if mod[0] != play[0] or mod[3] != play[3]:
        failures.append(f"modulate: modulation wrote automation (points {play[0]} -> {mod[0]}, "
                        f"envelopes {play[3]} -> {mod[3]})")
    # host-side playback: REAPER played the lane back.
    play_vals = [float(m.group(1)) for m in re.finditer(r"playback pos=[0-9.]+ param=([0-9.]+)", host_log)]
    facts["host_playback_param_range"] = [min(play_vals), max(play_vals)] if play_vals else None
    if not play_vals or min(play_vals) > 0.3:
        failures.append("playback: REAPER never played the recorded ramp back")

    samples = []
    for m in re.finditer(r"\[ui-sample\] (\{.*?\})", ui_log):
        try:
            samples.append(json.loads(m.group(1)))
        except json.JSONDecodeError:
            pass
    facts["ui_samples"] = len(samples)
    if len(samples) < 50:
        failures.append(f"editor: only {len(samples)} UI samples (fixture not running?)")
        return failures, facts
    by_pass = {0: [], -1: [], -2: [], -3: []}
    for s in samples:
        o = s.get("output")
        if isinstance(o, (int, float)) and round(o) in by_pass:
            by_pass[round(o)].append(s)
    rec_s, play_s, mod_s = by_pass[-1], by_pass[-2], by_pass[-3]
    rng = lambda xs, k: (min(x[k] for x in xs), max(x[k] for x in xs)) if xs else (None, None)
    facts["ui"] = {"record": rng(rec_s, "intensity"), "playback": rng(play_s, "intensity"),
                   "modulate_base": rng(mod_s, "intensity"),
                   "modulate_played": rng([s for s in mod_s if s.get("played") is not None], "played")}
    if not play_s or rng(play_s, "intensity")[0] > 30:
        failures.append(f"playback: the knob did not follow the lane (base {rng(play_s, 'intensity')})")
    # The pass marker (Output) and the LFO switch land in the editor a frame
    # or two apart at the pass boundary, so a stray sample there is not a
    # finding; a marker drawn through the pass is.
    stray = sum(1 for s in play_s if s.get("modOn"))
    if stray > 3:
        failures.append(f"playback: a modulated marker was drawn with no LFO on ({stray} samples)")
    played = [s for s in mod_s if s.get("played") is not None]
    if not played:
        failures.append("modulate: the knob drew no modulated value")
    else:
        lo, hi = rng(played, "played")
        if hi - lo < 20:
            failures.append(f"modulate: the modulated marker barely moved ({lo}..{hi})")
        if rng(mod_s, "intensity")[0] > 30:
            failures.append("modulate: the base did not follow the lane under modulation")
        if any(s["played"] > s["intensity"] + 0.01 for s in played):
            failures.append("modulate: a played value above its base (Intensity only pulls down)")
    return failures, facts


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
    fmt = {".clap": "clap", ".vst3": "vst3", ".component": "au"}[plugin.suffix]
    ns = argparse.Namespace(plugin_path=str(plugin), plugin_name=args.name, format=fmt,
                            timeout=args.timeout)
    session = smoke.ReaperSession(reaper, ns)
    try:
        if (code := session.place_plugin()) is not None:
            return code
        # Playback has to process: keep REAPER's audio device open while it is
        # not the frontmost app (a scripted session never is) and while
        # stopped. The master is muted, so nothing is heard.
        ini = session.portable / "reaper.ini"
        text = ini.read_text()
        for key in ("audiocloseinactive", "audioclosestop"):
            text = re.sub(rf"(?m)^{key}=.*\n", "", text)
        text, n = re.subn(r"(?im)^\[reaper\]\n",
                          lambda m: m.group(0) + "audiocloseinactive=0\naudioclosestop=0\n",
                          text, count=1)
        ini.write_text(text)
        status = session.portable / "status.txt"
        prefix = {"clap": "CLAP:", "vst3": "VST3:", "au": "AU:"}[fmt]
        env = dict(os.environ, PULP_DAW_SMOKE_FX=prefix + args.name,
                   PULP_DAW_SMOKE_STATUS=str(status), SPECTR_AUTO_OUT=str(out),
                   SPECTR_AUTO_PASS_SECONDS=str(PASS_SECONDS),
                   SPECTR_EVAL=EDITOR_JS.replace("%GESTURE_DELAY_MS%", str(GESTURE_DELAY_MS)))
        lua = session.portable / "modauto.lua"
        lua.write_text(LUA)
        if (code := session.run_until_fx_shown(env, status, lua)) is not None:
            log("REAPER did not show the FX: " + (status.read_text() if status.exists() else "no status"))
            return 1
        deadline = time.time() + 4 * PASS_SECONDS + 30
        while time.time() < deadline and not (out / "done.txt").exists():
            time.sleep(1.0)
        session.terminate()
        host_log = (out / "host.log").read_text() if (out / "host.log").exists() else ""
        ui_log = session.captured_log()
        (out / "reaper-stdout.txt").write_text(ui_log)
        failures, facts = analyse(host_log, ui_log)
        (out / "result.json").write_text(json.dumps({"format": fmt, "failures": failures,
                                                     "facts": facts}, indent=2))
        log(f"{fmt}: " + json.dumps(facts))
        for f in failures:
            log(f"FAIL {fmt}: {f}")
        log(f"{fmt}: {'PASS' if not failures else 'FAIL'}")
        return 0 if not failures else 1
    finally:
        session.cleanup()


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--plugin", required=True)
    ap.add_argument("--name", default="Spectr")
    ap.add_argument("--pulp-repo", default="~/Code/pulp")
    ap.add_argument("--out", default="reaper-modulation-automation")
    ap.add_argument("--timeout", type=float, default=120.0)
    return run(ap.parse_args())


if __name__ == "__main__":
    sys.exit(main())
