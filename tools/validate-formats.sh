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

resolve_artifact() {
    local format="$1" artifact="$2" candidate
    local preferred="${BUILD_DIR}/${format}/${artifact}"

    # Pulp's single-config generators use <build>/<FORMAT>/<artifact>.
    # Also accept common multi-config and legacy root layouts so this helper
    # keeps working with Ninja Multi-Config, Xcode, and older build trees.
    for candidate in \
        "$preferred" \
        "${BUILD_DIR}/${format}/Release/${artifact}" \
        "${BUILD_DIR}/Release/${format}/${artifact}" \
        "${BUILD_DIR}/Release/${artifact}" \
        "${BUILD_DIR}/${artifact}"
    do
        if [ -e "$candidate" ]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done

    # Preserve the expected path so install_copy emits one useful diagnostic.
    printf '%s\n' "$preferred"
}

AU_SRC="$(resolve_artifact AU Spectr.component)"
VST3_SRC="$(resolve_artifact VST3 Spectr.vst3)"
CLAP_SRC="$(resolve_artifact CLAP Spectr.clap)"

AU_DEST="${HOME}/Library/Audio/Plug-Ins/Components/Spectr.component"
VST3_DEST="${HOME}/Library/Audio/Plug-Ins/VST3/Spectr.vst3"
CLAP_DEST="${HOME}/Library/Audio/Plug-Ins/CLAP/Spectr.clap"

# AU codes from CMakeLists' pulp_add_plugin call.
AU_TYPE=aufx
AU_SUBTYPE=Spec
AU_MANU=Pulp

fail=0
passed=0
VALIDATOR_ENV=(
    env
    PULP_DISABLE_PLUGIN_EDITOR=1
    PULP_HEADLESS=1
    PULP_TEST_MODE=1
)
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
    if ! command -v auval >/dev/null 2>&1; then
        die "AU v2: auval not found"
    else
        say "Running auval -v ${AU_TYPE} ${AU_SUBTYPE} ${AU_MANU}"
        if "${VALIDATOR_ENV[@]}" auval -v "$AU_TYPE" "$AU_SUBTYPE" "$AU_MANU" \
            | tail -5 | grep -q "AU VALIDATION SUCCEEDED"; then
            say "AU v2: PASS"
            passed=$((passed + 1))
        else
            die "AU v2: FAIL (run auval -v ${AU_TYPE} ${AU_SUBTYPE} ${AU_MANU} for details)"
        fi
    fi
else
    die "AU v2: required artifact missing"
fi

# ── VST3 (pluginval) ───────────────────────────────────────────────────

if install_copy "$VST3_SRC" "$VST3_DEST" "VST3"; then
    if ! command -v pluginval >/dev/null 2>&1; then
        die "VST3: pluginval not found (brew install pluginval)"
    else
        say "Running pluginval --strictness-level 10 ${VST3_DEST}"
        if "${VALIDATOR_ENV[@]}" pluginval --strictness-level 10 \
            --validate "$VST3_DEST" 2>&1 | tail -5 | grep -qi "completed"; then
            say "VST3: PASS"
            passed=$((passed + 1))
        else
            die "VST3: FAIL"
        fi
    fi
else
    die "VST3: required artifact missing"
fi

# ── CLAP (clap-validator) ──────────────────────────────────────────────

if install_copy "$CLAP_SRC" "$CLAP_DEST" "CLAP"; then
    if ! command -v clap-validator >/dev/null 2>&1; then
        die "CLAP: clap-validator not found (cargo install clap-validator)"
    else
        say "Running clap-validator validate ${CLAP_DEST}"
        if "${VALIDATOR_ENV[@]}" clap-validator validate "$CLAP_DEST" 2>&1 \
            | tail -10 | grep -qi "passed\|success"; then
            say "CLAP: PASS"
            passed=$((passed + 1))
        else
            die "CLAP: FAIL"
        fi
    fi
else
    die "CLAP: required artifact missing"
fi

echo
if [ "$fail" -eq 0 ] && [ "$passed" -eq 3 ]; then
    echo "✓ All format validators succeeded."
    exit 0
else
    echo "✗ Format validation incomplete or failed (${passed}/3 passed)."
    exit 1
fi
