#!/usr/bin/env swift

import AppKit
import CoreGraphics
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("bands-perf-capture: \(message)\n".utf8))
    exit(1)
}

guard CommandLine.arguments.count == 4 || CommandLine.arguments.count == 5 else {
    fail("usage: bands_perf_capture.swift APP TRACE SCREENSHOT [bands|minimap|automation]")
}

let app = URL(fileURLWithPath: CommandLine.arguments[1])
let trace = URL(fileURLWithPath: CommandLine.arguments[2])
let screenshot = URL(fileURLWithPath: CommandLine.arguments[3])
let workload = CommandLine.arguments.count == 5 ? CommandLine.arguments[4] : "bands"
guard workload == "bands" || workload == "minimap" || workload == "automation"
        || workload == "lfo" || workload == "idle" else {
    fail("unknown workload: \(workload)")
}
let binary = app.appendingPathComponent("Contents/MacOS/Spectr")
guard FileManager.default.isExecutableFile(atPath: binary.path) else {
    fail("standalone binary is not executable: \(binary.path)")
}
try? FileManager.default.removeItem(at: trace)
try? FileManager.default.removeItem(at: screenshot)

let process = Process()
process.executableURL = binary
var environment = ProcessInfo.processInfo.environment
environment["PULP_TRACE_PATH"] = trace.path
environment["PULP_SCREENSHOT"] = screenshot.path
environment["PULP_FRAMES"] = "420"
if workload == "lfo" || workload == "idle" {
    // The LFO arm is the reference comparison for modulation smoothness, and
    // the idle arm is its interleaved control. Both must keep audio live: the
    // LFO phase only advances inside Spectr::process(), so a screenshot launch
    // that tears the audio system down measures a frozen modulator rather than
    // the feature. The synthesized zero-amplitude source keeps the capture
    // silent and off the machine's real input.
    environment["PULP_SCREENSHOT_KEEP_AUDIO"] = "1"
    environment["PULP_TEST_SIGNAL"] = "sine"
    environment["PULP_TEST_SIGNAL_AMPLITUDE"] = "0"
    if workload == "lfo" {
        environment["SPECTR_LFO_PERF_FIXTURE"] = "1"
        if let shape = ProcessInfo.processInfo.environment["SPECTR_LFO_PERF_SHAPE"] {
            environment["SPECTR_LFO_PERF_SHAPE"] = shape
        }
    }
} else {
    environment["SPECTR_BANDS_PERF_FIXTURE"] = "1"
    if workload == "automation" {
        environment["SPECTR_AUTOMATION_PERF_FIXTURE"] = "1"
    } else {
        environment["PULP_TEST_POINTER_DRAG"] = workload == "minimap" ? "minimap" : "1"
    }
}
process.environment = environment
try process.run()

func ownedWindow(pid: pid_t) -> CGRect? {
    guard let windows = CGWindowListCopyWindowInfo([.optionAll, .excludeDesktopElements],
                                                    kCGNullWindowID) as? [[String: Any]] else {
        return nil
    }
    for window in windows {
        guard (window[kCGWindowOwnerPID as String] as? NSNumber)?.int32Value == pid,
              (window[kCGWindowLayer as String] as? NSNumber)?.intValue == 0,
              let bounds = window[kCGWindowBounds as String] as? [String: CGFloat],
              let x = bounds["X"], let y = bounds["Y"],
              let width = bounds["Width"], let height = bounds["Height"],
              width > 600, height > 400 else { continue }
        return CGRect(x: x, y: y, width: width, height: height)
    }
    return nil
}

var frame: CGRect?
for _ in 0..<120 {
    frame = ownedWindow(pid: process.processIdentifier)
    if frame != nil { break }
    usleep(25_000)
}
guard let window = frame else {
    process.terminate()
    fail("no owned app window for pid \(process.processIdentifier)")
}

let samples = 180
process.waitUntilExit()
guard process.terminationStatus == 0 else {
    fail("standalone exited with status \(process.terminationStatus)")
}
guard let traceSize = (try? FileManager.default.attributesOfItem(atPath: trace.path)[.size])
        as? NSNumber, traceSize.intValue > 4096 else {
    fail("trace was not flushed or is empty: \(trace.path)")
}
guard let screenshotSize = (try? FileManager.default.attributesOfItem(atPath: screenshot.path)[.size])
        as? NSNumber, screenshotSize.intValue > 1024 else {
    fail("GPU screenshot was not produced: \(screenshot.path)")
}
print("pid=\(process.processIdentifier) window=\(NSStringFromRect(window)) workload=\(workload) samples=\(samples) trace_bytes=\(traceSize) screenshot_bytes=\(screenshotSize)")
