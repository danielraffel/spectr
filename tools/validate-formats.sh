#!/usr/bin/env bash
# validate-formats.sh — run auval / pluginval / clap-validator against
# Spectr's three format builds. Milestone 10 deliverable.
#
# Assumes cmake has built the plugins into ./build. Installs AU/VST3/CLAP
# into the host-scanned folders (~/Library/Audio/Plug-Ins/<FMT>/), then
# runs each validator.
#
# Exit 0 on all green. Non-zero if any validator reports a failure.
# Designed for local runs; the format validation lane in CI (when it
# exists) can shell out to this.

set -euo pipefail

BUILD_DIR="${1:-$(pwd)/build}"

AU_SRC="${BUILD_DIR}/Spectr.component"
VST3_SRC="${BUILD_DIR}/Spectr.vst3"
CLAP_SRC="${BUILD_DIR}/Spectr.clap"

AU_DEST="${HOME}/Library/Audio/Plug-Ins/Components/Spectr.component"
VST3_DEST="${HOME}/Library/Audio/Plug-Ins/VST3/Spectr.vst3"
CLAP_DEST="${HOME}/Library/Audio/Plug-Ins/CLAP/Spectr.clap"

# AU codes from CMakeLists' pulp_add_plugin call.
AU_TYPE=aufx
AU_SUBTYPE=Spec
AU_MANU=Pulp

fail=0
say()  { printf "\n▸ %s\n" "$*"; }
warn() { printf "⚠  %s\n" "$*" >&2; }
die()  { printf "✗ %s\n" "$*" >&2; fail=1; }

install_copy() {
    local src="$1" dst="$2" label="$3"
    if [ ! -e "$src" ]; then
        warn "${label}: source missing (${src}) — skipping install"
        return 1
    fi
    mkdir -p "$(dirname "$dst")"
    rm -rf "$dst"
    cp -R "$src" "$dst"
    say "${label} installed: ${dst}"
    return 0
}

# ── AU (auval, built in) ───────────────────────────────────────────────

if install_copy "$AU_SRC" "$AU_DEST" "AU v2"; then
    say "Running auval -v ${AU_TYPE} ${AU_SUBTYPE} ${AU_MANU}"
    if auval -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANU" | tail -5 | grep -q "AU VALIDATION SUCCEEDED"; then
        say "AU v2: PASS"
    else
        die "AU v2: FAIL (see \`auval -v ${AU_TYPE} ${AU_SUBTYPE} ${AU_MANU}\` for details)"
    fi
fi

# ── VST3 (pluginval) ───────────────────────────────────────────────────

if install_copy "$VST3_SRC" "$VST3_DEST" "VST3"; then
    if ! command -v pluginval >/dev/null 2>&1; then
        warn "pluginval not found (brew install pluginval); skipping VST3 validation"
    else
        say "Running pluginval --strictness-level 10 ${VST3_DEST}"
        if pluginval --strictness-level 10 --validate "$VST3_DEST" 2>&1 | tail -5 | grep -qi "completed"; then
            say "VST3: PASS"
        else
            die "VST3: FAIL"
        fi
    fi
fi

# ── CLAP (clap-validator) ──────────────────────────────────────────────

if install_copy "$CLAP_SRC" "$CLAP_DEST" "CLAP"; then
    if ! command -v clap-validator >/dev/null 2>&1; then
        warn "clap-validator not found (cargo install clap-validator); skipping CLAP validation"
    else
        say "Running clap-validator validate ${CLAP_DEST}"
        if clap-validator validate "$CLAP_DEST" 2>&1 | tail -10 | grep -qi "passed\|success"; then
            say "CLAP: PASS"
        else
            die "CLAP: FAIL"
        fi
    fi
fi

echo
if [ "$fail" -eq 0 ]; then
    echo "✓ All format validators succeeded."
    exit 0
else
    echo "✗ One or more validators failed."
    exit 1
fi
