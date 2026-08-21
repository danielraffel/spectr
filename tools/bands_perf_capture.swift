#!/usr/bin/env swift

import AppKit
import CoreGraphics
import Foundation

func fail(_ message: String) -> Never {
    FileHandle.standardError.write(Data("bands-perf-capture: \(message)\n".utf8))
    exit(1)
}

guard CommandLine.arguments.count == 4 else {
    fail("usage: bands_perf_capture.swift APP TRACE SCREENSHOT")
}

let app = URL(fileURLWithPath: CommandLine.arguments[1])
let trace = URL(fileURLWithPath: CommandLine.arguments[2])
let screenshot = URL(fileURLWithPath: CommandLine.arguments[3])
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
environment["SPECTR_BANDS_PERF_FIXTURE"] = "1"
environment["PULP_TEST_POINTER_DRAG"] = "1"
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
print("pid=\(process.processIdentifier) window=\(NSStringFromRect(window)) samples=\(samples) trace_bytes=\(traceSize) screenshot_bytes=\(screenshotSize)")
