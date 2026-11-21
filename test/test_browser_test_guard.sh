#!/bin/sh
# The browser test guard skips only the installed browser inside a sandbox and
# otherwise runs the test command unchanged.
guard="$(dirname "$0")/../tools/browser_test_guard.sh"
work=$(mktemp -d) || exit 1
trap 'rm -f "$work/ran"; rmdir "$work"' EXIT
installed="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome"
fail() { echo "FAIL: $1"; exit 1; }
# Records that it ran; its browser argument is passed through but unused.
mark='touch "$0"'

CODEX_SANDBOX=seatbelt /bin/sh "$guard" /bin/sh -c "$mark" "$work/ran" "$installed"
[ $? -eq 77 ] || fail "sandbox + installed browser did not skip"
[ ! -e "$work/ran" ] || fail "sandbox + installed browser still ran the command"

CODEX_SANDBOX=seatbelt /bin/sh "$guard" /bin/sh -c "$mark" "$work/ran" /opt/chrome-headless-shell
[ $? -eq 0 ] && [ -e "$work/ran" ] || fail "sandbox + pinned headless shell did not run"
rm -f "$work/ran"

env -u CODEX_SANDBOX /bin/sh "$guard" /bin/sh -c "$mark" "$work/ran" "$installed"
[ $? -eq 0 ] && [ -e "$work/ran" ] || fail "outside a sandbox the installed browser did not run"
rm -f "$work/ran"

env -u CODEX_SANDBOX /bin/sh "$guard" /bin/sh -c 'exit 3'
[ $? -eq 3 ] || fail "the test's own exit status was not passed through"
echo "browser test guard: 4 checks passed"
