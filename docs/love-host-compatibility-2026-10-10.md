# LÖVE host compatibility decision

## Evidence

The current Spectr checkout contains no LÖVE or `love2d` host adapter, CMake
target, runtime dependency, example, or test. The Pulp SDK checkout used for
the release build likewise contains no LÖVE integration. The release artifacts
are native Standalone, AU v2, VST3, and CLAP products.

The verified host contract is therefore the native plugin formats and the
standalone target. REAPER and Logic/AU validation exercise those contracts;
LÖVE cannot be treated as another plugin host without a new bridge.

## Decision

LÖVE is **unsupported and unverified** for this release. Do not add a browser,
Lua, or second renderer merely to create a LÖVE path. If a future product needs
LÖVE, the smallest compatible experiment should be a separate native LÖVE
module or helper process that consumes the public parameter/state API and never
becomes an audio-thread authority. That experiment needs its own scope,
latency, packaging, and real-host acceptance criteria.

This is an explicit compatibility decision, not evidence that a future bridge
would be impossible.
