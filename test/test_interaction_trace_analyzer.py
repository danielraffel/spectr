#!/usr/bin/env python3

import importlib.util
from pathlib import Path
import unittest


MODULE_PATH = Path(__file__).parents[1] / "tools" / "analyze_interaction_trace.py"
SPEC = importlib.util.spec_from_file_location("analyze_interaction_trace", MODULE_PATH)
assert SPEC and SPEC.loader
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)


class InteractionTraceAnalyzerTest(unittest.TestCase):
    def test_query_names_real_pulp_interaction_and_frame_slices(self) -> None:
        query = MODULE.build_query()
        self.assertIn("native_drag_dispatch", query)
        self.assertIn("s.category = 'render' AND s.name = 'frame'", query)
        self.assertIn("layout_children", query)
        self.assertIn("gpu_present", query)

    def test_query_excludes_idle_settle_paints_from_overdraw(self) -> None:
        query = MODULE.build_query()
        self.assertIn("w.name NOT IN ('layout_children', 'paint')", query)
        self.assertIn("ABS(s.ts - i.ts) <= 17000000", query)

    def test_parse_output_preserves_percentiles_and_stages(self) -> None:
        output = """
SPECTR_INTERACTION_PERF|summary|180|0.250000|0.500000|181|4.000000|7.900000|8.200000|8.300000|0|0
SPECTR_INTERACTION_PERF|stage|layout_children|180|20.000000|0.200000
SPECTR_INTERACTION_PERF|stage|paint|180|100.000000|1.000000
SPECTR_INTERACTION_PERF|stage|dom_event_dispatch|180|10.000000|0.100000
SPECTR_INTERACTION_PERF|stage|gpu_acquire|181|20.000000|0.200000
SPECTR_INTERACTION_PERF|stage|gpu_submit|181|20.000000|0.200000
SPECTR_INTERACTION_PERF|stage|gpu_present|181|20.000000|0.200000
"""
        summary, stages = MODULE.parse_output(output)
        self.assertEqual(summary["input_count"], 180)
        self.assertEqual(summary["frame_p95_ms"], 7.9)
        self.assertEqual(stages["paint"]["count"], 180)
        self.assertEqual(MODULE.failures_for("bands", summary, stages, 1.0, 8.5, 16.667), [])

    def test_missing_gpu_and_excess_layout_churn_fail_closed(self) -> None:
        summary = {
            "input_count": 180,
            "input_p95_ms": 0.25,
            "input_max_ms": 0.5,
            "frame_count": 180,
            "frame_p50_ms": 4.0,
            "frame_p95_ms": 7.9,
            "frame_p99_ms": 8.2,
            "frame_max_ms": 8.3,
            "frames_over_120hz": 0,
            "frames_over_60hz": 0,
        }
        stages = {
            "layout_children": {"count": 250, "total_ms": 20.0, "max_ms": 0.2},
            "paint": {"count": 180, "total_ms": 100.0, "max_ms": 1.0},
            "dom_event_dispatch": {"count": 180, "total_ms": 10.0, "max_ms": 0.1},
        }
        failures = MODULE.failures_for("bands", summary, stages, 1.0, 8.5, 16.667)
        self.assertTrue(any("missing required stage" in failure for failure in failures))
        self.assertTrue(any("layout_children/input" in failure for failure in failures))

    def test_minimap_requires_all_three_sixty_sample_gestures(self) -> None:
        summary = {
            "input_count": 120,
            "input_p95_ms": 0.25,
            "input_max_ms": 0.5,
            "frame_count": 180,
            "frame_p50_ms": 4.0,
            "frame_p95_ms": 7.9,
            "frame_p99_ms": 8.2,
            "frame_max_ms": 8.3,
            "frames_over_120hz": 0,
            "frames_over_60hz": 0,
        }
        stages = {name: {"count": 120, "total_ms": 1.0, "max_ms": 0.1}
                  for name in ("layout_children", "paint", "dom_event_dispatch",
                               "gpu_acquire", "gpu_submit", "gpu_present")}
        failures = MODULE.failures_for("minimap", summary, stages, 1.0, 8.5, 16.667)
        self.assertTrue(any("below 150 for minimap" in failure for failure in failures))


if __name__ == "__main__":
    unittest.main()
