param(
    [string]$BuildDir = "build/default",
    [string]$Configuration = "Debug",
    [switch]$SkipBuild,
    [switch]$IncludeRedisLive,
    [switch]$IncludeOperatorKind,
    [switch]$IncludeRuntimeHttp,
    [switch]$IncludeReleaseBaseline,
    [switch]$IncludeCapacityBaseline,
    [ValidateSet("smoke", "short", "medium")]
    [string]$SoakProfile = "smoke",
    [ValidateSet("debug", "release")]
    [string]$BaselineProfile = "debug",
    [int]$PerfRepetitions = 1,
    [int]$StepTimeoutSeconds = 900,
    [int]$KindTimeoutSeconds = 900,
    [string]$SummaryPath = "runtime/validation/production-resilience-summary.json"
)

$ErrorActionPreference = "Stop"
$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$repoRoot = Split-Path -Parent $scriptDir

$argsList = @(
    "$scriptDir/verify_production_resilience_gate.py",
    "--build-dir", $BuildDir,
    "--configuration", $Configuration,
    "--soak-profile", $SoakProfile,
    "--baseline-profile", $BaselineProfile,
    "--perf-repetitions", "$PerfRepetitions",
    "--step-timeout-seconds", "$StepTimeoutSeconds",
    "--kind-timeout-seconds", "$KindTimeoutSeconds",
    "--summary-path", $SummaryPath
)

if ($SkipBuild) { $argsList += "--skip-build" }
if ($IncludeRedisLive) { $argsList += "--include-redis-live" }
if ($IncludeOperatorKind) { $argsList += "--include-operator-kind" }
if ($IncludeRuntimeHttp) { $argsList += "--include-runtime-http" }
if ($IncludeReleaseBaseline) { $argsList += "--include-release-baseline" }
if ($IncludeCapacityBaseline) { $argsList += "--include-capacity-baseline" }

Push-Location $repoRoot
try {
    python @argsList
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
