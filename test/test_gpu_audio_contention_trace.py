#!/usr/bin/env python3

import importlib.util
import unittest
from pathlib import Path

MODULE = Path(__file__).parents[1] / "tools" / "gpu_audio_contention_trace.py"
SPEC = importlib.util.spec_from_file_location("gpu_audio_contention_trace", MODULE)
assert SPEC and SPEC.loader
TRACE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(TRACE)


class GpuAudioContentionTraceTest(unittest.TestCase):
    def test_every_admitted_block_has_one_terminal_disposition(self) -> None:
        rows = [
            {"event": "gpu_audio_admission", "stream_epoch": 4, "block_sequence": 8},
            {"event": "gpu_audio_submit", "stream_epoch": 4, "block_sequence": 8},
            {"event": "gpu_audio_completion", "stream_epoch": 4, "block_sequence": 8},
            {"event": "gpu_audio_terminal", "stream_epoch": 4, "block_sequence": 8,
             "terminal_disposition": "gpu_delivered"},
            {"event": "gpu_audio_admission", "stream_epoch": 4, "block_sequence": 9},
            {"event": "gpu_audio_terminal", "stream_epoch": 4, "block_sequence": 9,
             "terminal_disposition": "cpu_fallback"},
        ]
        summary = TRACE.validate_rows(rows)
        self.assertEqual(summary["admitted_blocks"], 2)
        self.assertEqual(summary["terminal_dispositions"],
                         {"cpu_fallback": 1, "gpu_delivered": 1})

    def test_orphaned_block_is_rejected(self) -> None:
        with self.assertRaisesRegex(ValueError, "without terminal"):
            TRACE.validate_rows([{"event": "gpu_audio_admission",
                                  "stream_epoch": 1, "block_sequence": 2}])

    def test_duplicate_terminal_and_unknown_disposition_are_rejected(self) -> None:
        rows = [
            {"event": "gpu_audio_admission", "stream_epoch": 1, "block_sequence": 2},
            {"event": "gpu_audio_terminal", "stream_epoch": 1, "block_sequence": 2,
             "terminal_disposition": "silence"},
        ]
        with self.assertRaisesRegex(ValueError, "duplicate terminal"):
            TRACE.validate_rows(rows + [rows[-1]])
        with self.assertRaisesRegex(ValueError, "unknown terminal"):
            TRACE.validate_rows([rows[0], {
                "event": "gpu_audio_terminal", "stream_epoch": 1, "block_sequence": 2,
                "terminal_disposition": "bogus"}])


if __name__ == "__main__":
    unittest.main()
