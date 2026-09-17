#!/usr/bin/env python3
"""Sync the product bridge replay from its maintained source without rebundling."""
import argparse
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'native-ui/materialized/spectr-native-services.js'
RUNTIME = ROOT / 'native-ui/materialized/runtime.js'
START = '  (function replayMaterializedProductPrelude('
END = '  if (g5.window && g5.__pulpMaterializedWindowGlobals__'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    text = RUNTIME.read_text()
    assert text.count(START) == 1 and text.count(END) == 1
    start, end = text.index(START), text.index(END)
    assert start < end
    replay = ('  (function replayMaterializedProductPrelude(globalThis) {\n'
              '    const window = globalThis.window || globalThis;\n'
              + SOURCE.read_text() + '\n  })(g5);\n')
    updated = text[:start] + replay + text[end:]
    if updated != text:
        if args.check:
            raise SystemExit('Materialized native bridge replay is stale; run this script without --check.')
        RUNTIME.write_text(updated)
    print('Materialized native bridge replay matches source.')


if __name__ == '__main__':
    main()
