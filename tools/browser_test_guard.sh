#!/bin/sh
# Runs a browser test, or skips it (exit 77) when it would start the installed
# Google Chrome inside an agent sandbox. That browser registers with the window
# server even when headless; a sandbox denies the registration, Chrome aborts,
# and macOS files a crash report for the user's own browser. Point
# SPECTR_CHROME_EXECUTABLE at a headless shell to run these tests in a sandbox.
if [ -n "${CODEX_SANDBOX:-}" ]; then
    for argument in "$@"; do
        case "$argument" in
            /Applications/*.app/Contents/MacOS/*)
                echo "SKIP: the installed browser cannot start inside a sandbox ($argument)"
                exit 77
                ;;
        esac
    done
fi
exec "$@"
