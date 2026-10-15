# Maps a packaged plug-in architecture to the REAPER host installation used for
# discovery. ARM64EC plug-ins run in native ARM64 REAPER and therefore use the
# ARM64 cache and executable paths.
function Get-ReaperHostArchitecture {
    param([Parameter(Mandatory = $true)] [string]$Architecture)

    switch ($Architecture) {
        'x86_64-win' { return 'x86_64-win' }
        'arm64-win' { return 'arm64-win' }
        'arm64ec-win' { return 'arm64-win' }
        default { throw "Unsupported REAPER plug-in architecture: $Architecture" }
    }
}
