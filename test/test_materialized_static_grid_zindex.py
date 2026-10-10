#!/usr/bin/env python3
"""Regression control for the browser-visible materialized static grid layer."""
import hashlib
import json
import os
import subprocess
import tempfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]
ARTIFACT = REPO / 'native-ui' / 'materialized' / 'materialized-document.runtime.json'
PATCH = REPO / 'tools' / 'patch_materialized_static_grid_canvas.py'

def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main() -> int:
    document = json.loads(ARTIFACT.read_text())
    html = document['html']
    marker = 'data-spectr-static-canvas'
    assert html.count(marker) == 1, 'static canvas marker must be unique'
    start = html.index(marker)
    snippet = html[start:start + 260]
    assert 'zIndex: 0' in snippet, 'static canvas must be above the parent background'
    assert 'zIndex: -1' not in snippet, 'static canvas must not be hidden behind the parent'
    filter_at = html.index('data-spectr-filter-canvas')
    overlay_at = html.index('ref: overlayRef')
    static_at = html.index('data-spectr-static-canvas')
    assert filter_at < overlay_at < static_at, 'canvas DOM order must remain filter, overlay, static'
    filter_snippet = html[filter_at:filter_at + 220]
    overlay_snippet = html[overlay_at:overlay_at + 180]
    assert 'zIndex: 1' in filter_snippet, 'dynamic layer must be above static'
    assert 'zIndex: 2' in overlay_snippet, 'overlay must be above dynamic'
    before = digest(ARTIFACT)
    result = subprocess.run(['python3', str(PATCH)], cwd=REPO,
                            text=True, capture_output=True, check=True)
    after = digest(ARTIFACT)
    assert before == after, f'idempotent patch changed artifact: {before} -> {after}'
    assert 'already current' in result.stdout, result.stdout

    malformed = json.loads(ARTIFACT.read_text())
    malformed['html'] = malformed['html'].replace('zIndex: 0', 'zIndex: 3', 1)
    with tempfile.NamedTemporaryFile('w', suffix='.json') as handle:
        json.dump(malformed, handle, separators=(',', ':'))
        handle.flush()
        control = subprocess.run(
            ['python3', str(PATCH)], cwd=REPO,
            env={**os.environ, 'SPECTR_MATERIALIZED_ARTIFACT': handle.name},
            text=True, capture_output=True)
    assert control.returncode != 0, control.stdout + control.stderr
    assert 'FAIL' in control.stderr, control.stdout + control.stderr

    duplicate = json.loads(ARTIFACT.read_text())
    static = 'data-spectr-static-canvas'
    duplicate['html'] = duplicate['html'].replace(
        static, static + ' ' + static, 1)
    with tempfile.NamedTemporaryFile('w', suffix='.json') as handle:
        json.dump(duplicate, handle, separators=(',', ':'))
        handle.flush()
        duplicate_control = subprocess.run(
            ['python3', str(PATCH)], cwd=REPO,
            env={**os.environ, 'SPECTR_MATERIALIZED_ARTIFACT': handle.name},
            text=True, capture_output=True)
    assert duplicate_control.returncode != 0, (
        duplicate_control.stdout + duplicate_control.stderr)
    assert 'FAIL' in duplicate_control.stderr, (
        duplicate_control.stdout + duplicate_control.stderr)
    print('PASS: materialized static grid z-index is browser-visible and patch is idempotent')
    return 0

if __name__ == '__main__':
    raise SystemExit(main())
