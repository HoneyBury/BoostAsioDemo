param(
    [string]$BuildDir = "build/windows-msvc-debug",
    [string]$Configuration = "Debug",
    [switch]$SkipBuild,
    [switch]$SkipArchBaseline
)

$ErrorActionPreference = "Stop"

$argsList = @(
    "scripts/verify_r4_contract.py",
    "--build-dir", $BuildDir,
    "--configuration", $Configuration
)

if ($SkipBuild) {
    $argsList += "--skip-build"
}
if ($SkipArchBaseline) {
    $argsList += "--skip-arch-baseline"
}

python @argsList
