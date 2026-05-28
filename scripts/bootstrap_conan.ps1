$ErrorActionPreference = "Stop"

$projectRoot = Split-Path -Parent $PSScriptRoot
$wrapperPath = Join-Path $projectRoot "scripts\wrappers\ps1\bootstrap_conan.ps1"

& powershell -ExecutionPolicy Bypass -File $wrapperPath @args
exit $LASTEXITCODE
