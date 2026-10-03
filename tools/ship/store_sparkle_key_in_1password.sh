#!/usr/bin/env bash
# Store Spectr's Sparkle EdDSA private key in 1Password, then prove the stored
# copy matches the local file. Never prints the key: it travels from the file
# to `op` on stdin (a JSON item template), never on a command line.
#
#   tools/ship/store_sparkle_key_in_1password.sh [vault]   # default vault: Private
set -euo pipefail

VAULT="${1:-Private}"
TITLE="Spectr Sparkle EdDSA private key"
KEY="${SPECTR_SPARKLE_KEY_FILE:-$HOME/.config/pulp/secrets/sparkle/spectr_ed25519}"
PUBLIC="mosCtB7H9gxWzbWUYyHiHTapl4sWMgkd4t09iIUnO2g="

[[ -f "$KEY" ]] || { echo "no key at $KEY" >&2; exit 2; }
command -v op >/dev/null || { echo "1Password CLI (op) is not installed" >&2; exit 2; }
op whoami >/dev/null 2>&1 || {
  echo "op is not signed in: unlock the 1Password app (Settings > Developer >" >&2
  echo "Integrate with 1Password CLI) or run 'eval \$(op signin)', then re-run." >&2
  exit 2
}
if op item get "$TITLE" --vault "$VAULT" >/dev/null 2>&1; then
  echo "an item named '$TITLE' already exists in vault $VAULT; not overwriting" >&2
else
  jq -n --rawfile k "$KEY" --arg pub "$PUBLIC" --arg title "$TITLE" '{
    title: $title,
    category: "SECURE_NOTE",
    notesPlain: "Sparkle 2 EdDSA (Ed25519) signing key for Spectr.app updates. 32-byte seed, base64 (generate_keys -x format). Public key: SUPublicEDKey in danielraffel/spectr cmake/SpectrSparkle.cmake. Local copy: ~/.config/pulp/secrets/sparkle/spectr_ed25519 (chmod 600). Losing it strands every installed copy.",
    fields: [
      {id: "private_key", label: "private key", type: "CONCEALED", value: ($k | rtrimstr("\n"))},
      {id: "public_key", label: "public key", type: "STRING", value: $pub}
    ]}' | op item create --vault "$VAULT" >/dev/null
  echo "created '$TITLE' in vault $VAULT"
fi
if cmp -s <(op read "op://$VAULT/$TITLE/private key" | tr -d '\n') <(tr -d '\n' < "$KEY"); then
  echo "verified: the 1Password copy matches $KEY"
else
  echo "MISMATCH: the 1Password copy differs from $KEY" >&2
  exit 1
fi
