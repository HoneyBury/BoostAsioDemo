param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$Configuration = "Debug",
    [string]$BaselineProfile = "debug",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$argsList = @(
    "scripts/verify_stability_soak.py",
    "--build-dir", $BuildDir,
    "--configuration", $Configuration,
    "--baseline-profile", $BaselineProfile
)

if ($SkipBuild) {
    $argsList += "--skip-build"
}

python @argsList
