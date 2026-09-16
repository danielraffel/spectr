#!/usr/bin/env bash
# Enable the materialized-runtime merge driver in this clone.
#
# A merge driver cannot be configured by a checked-in file alone: .gitattributes
# names the driver, but the command it runs must live in local git config,
# because git will not execute a command a repository can inject. So every
# clone (and every worktree's parent repo) runs this once.
#
# Safe to re-run; safe to skip. Without it git just conflicts on the artifact
# the way it always has.
#
# This deliberately does NOT write the path rule into $GIT_DIR/info/attributes,
# which would make the driver apply on every branch rather than only on branches
# that carry .gitattributes. That sounds like an improvement and is a trap: when
# a merge driver's COMMAND fails -- which is what happens on any branch that
# predates tools/git/merge_materialized_runtime.py, since the configured path
# points into the working tree -- git reports a conflict but leaves the file
# byte-identical to ours with NO conflict markers. A `git add` on that reads as
# resolving a conflict and silently discards the other side's entire change,
# which is the failure this driver exists to prevent. Keeping the rule in the
# tracked .gitattributes makes it self-consistent: the attribute and the script
# it names are added, changed and removed by the same commit, so the attribute
# can never be live without its driver.
set -euo pipefail

repo_root="$(git rev-parse --show-toplevel)"
driver="${repo_root}/tools/git/merge_materialized_runtime.py"

if [[ ! -x "${driver}" ]]; then
    echo "error: driver not found or not executable: ${driver}" >&2
    exit 1
fi

python_bin="$(command -v python3 || true)"
if [[ -z "${python_bin}" ]]; then
    echo "error: python3 is required for the materialized-runtime merge driver" >&2
    exit 1
fi

git config merge.materialized-runtime.name \
    "three-way merge of the materialized editor document's decoded html payload"
git config merge.materialized-runtime.driver \
    "${python_bin} ${driver} %O %A %B %L %P"

echo "enabled: merge.materialized-runtime -> ${driver}"
echo
echo "Verify with:  python3 tools/git/test_merge_materialized_runtime.py"
