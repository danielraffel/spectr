#!/usr/bin/env python3
"""Positive and planted-negative controls for the generated Spectr client."""
from __future__ import annotations

import json
import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "tools" / "generate_spectr_runtime_client.mjs"
SERVICES = ROOT / "native-ui" / "materialized" / "spectr-native-services.js"
BRIDGE = ROOT / "src" / "editor_bridge.cpp"
EDITOR_VIEW = ROOT / "src" / "ui" / "editor_view.cpp"


def run_generator(out: pathlib.Path, *, bridge: pathlib.Path = BRIDGE,
                  services: pathlib.Path = SERVICES,
                  verify: bool = False) -> subprocess.CompletedProcess[str]:
    args = ["node", str(GENERATOR), "--bridge", str(bridge),
            "--bridge", str(EDITOR_VIEW), "--services", str(services),
            "--out", str(out)]
    if verify:
        args.append("--verify")
    return subprocess.run(args, cwd=ROOT, text=True, capture_output=True,
                          check=False, timeout=30)


class RuntimeClientContractTest(unittest.TestCase):
    def test_generation_is_deterministic_and_client_dispatches(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            first, second = root / "first", root / "second"
            result = run_generator(first)
            self.assertEqual(result.returncode, 0, result.stderr)
            result = run_generator(second)
            self.assertEqual(result.returncode, 0, result.stderr)
            names = ["spectr-runtime-client.mjs", "spectr-runtime-client.d.ts",
                     "spectr-runtime-client.manifest.json"]
            for name in names:
                self.assertEqual((first / name).read_bytes(), (second / name).read_bytes(), name)
            manifest = json.loads((first / names[2]).read_text())
            self.assertEqual(manifest["schema"], "spectr-generated-runtime-client-v1")
            self.assertEqual(manifest["version"], 1)
            handlers = {entry["name"] for entry in manifest["handlers"]}
            self.assertIn("processing_state_get", handlers)
            self.assertIn("spectral_resolution_request", handlers)
            self.assertEqual(manifest["service_references"]["strict"], [
                "macro_set_members", "processing_state_get", "processing_state_set",
                "redo", "spectral_resolution_request", "undo", "undo_gesture_end",
            ])

            probe = root / "probe.mjs"
            probe.write_text(
                "import { createSpectrRuntimeClient, SPECTR_COMMANDS } from "
                + repr((first / "spectr-runtime-client.mjs").as_uri())
                + ";\n"
                + "const seen=[]; const client=createSpectrRuntimeClient((type,payload,id)=>"
                + "{seen.push({type,payload,id}); return {ok:true,payload:{type}};});\n"
                + "const result=await client.processingStateGet({probe:true},'probe-id');\n"
                + "if (!result.ok || seen[0].type !== 'processing_state_get' || seen[0].id !== 'probe-id') throw new Error('typed client dispatch mismatch');\n"
                + "if (!SPECTR_COMMANDS.includes('param_set')) throw new Error('command union missing param_set');\n"
                + "let rejected=false; try { await client.request('unknown_command'); } catch (error) { rejected=/unknown Spectr command/.test(error.message); }\n"
                + "if (!rejected) throw new Error('unknown command was not rejected');\n"
            )
            probe_result = subprocess.run(["node", str(probe)], cwd=ROOT,
                                          text=True, capture_output=True,
                                          check=False, timeout=30)
            self.assertEqual(probe_result.returncode, 0, probe_result.stderr)

    def test_verify_accepts_checked_in_output(self):
        checked_in = ROOT / "native-ui" / "materialized" / "generated"
        result = run_generator(checked_in, verify=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn('"verified": true', result.stdout)

    def test_typescript_declaration_accepts_typed_client_usage(self):
        tsc = ROOT / "tools" / "wp1-parser" / "node_modules" / "typescript" / "bin" / "tsc"
        if not tsc.exists():
            self.skipTest("pinned TypeScript toolchain is not installed")
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            source = ROOT / "native-ui" / "materialized" / "generated" / "spectr-runtime-client.d.ts"
            (root / "spectr-runtime-client.d.ts").write_bytes(source.read_bytes())
            (root / "consumer.ts").write_text(
                "import { createSpectrRuntimeClient, type SpectrCommand } from './spectr-runtime-client';\n"
                "const dispatch = (type: SpectrCommand, payload = {}, id = '') => ({ ok: true, payload: { type, payload, id } });\n"
                "const client = createSpectrRuntimeClient(dispatch);\n"
                "async function probe() { const result = await client.processingStateGet<{ revision: number }>();\n"
                "  if (!result.ok || result.payload?.revision === undefined) throw new Error('typed response missing'); }\n"
                "void probe();\n"
            )
            result = subprocess.run([
                str(tsc), "--strict", "--noEmit", "--target", "ES2020",
                "--module", "commonjs", "--skipLibCheck", "consumer.ts",
            ], cwd=root, text=True, capture_output=True, check=False, timeout=30)
            self.assertEqual(result.returncode, 0, result.stderr or result.stdout)

    def test_missing_cpp_handler_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            bridge = root / "editor_bridge.cpp"
            bridge.write_text(BRIDGE.read_text().replace(
                'bridge.add_handler("processing_state_get"',
                'bridge.add_handler("processing_state_missing"', 1))
            result = run_generator(root / "out", bridge=bridge)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("processing_state_get", result.stderr)
            self.assertIn("unregistered handler", result.stderr)

    def test_unknown_service_command_fails_closed(self):
        with tempfile.TemporaryDirectory() as td:
            root = pathlib.Path(td)
            services = root / "services.js"
            services.write_text(SERVICES.read_text().replace(
                "dispatch('processing_state_get'",
                "dispatch('processing_state_missing'", 1))
            result = run_generator(root / "out", services=services)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("processing_state_missing", result.stderr)
            self.assertIn("unregistered handler", result.stderr)


if __name__ == "__main__":
    unittest.main()
