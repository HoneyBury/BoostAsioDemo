# Boost Gateway — 混沌稳定性测试运行脚本（Windows）
#
# 运行方式：
#   .\scripts\run_chaos_soak.ps1 [-BuildDir <path>] [-ConfigFile <path>] [-SkipBuild]
#
# 参数：
#   -BuildDir    构建输出目录（默认 build/windows-msvc-chaos）
#   -ConfigFile  混沌测试配置文件（默认 runtime/validation/configs/soak-chaos.json）
#   -Configuration 构建配置（默认 Release）
#   -SkipBuild   跳过 cmake 构建步骤
#
# 示例：
#   .\scripts\run_chaos_soak.ps1
#   .\scripts\run_chaos_soak.ps1 -SkipBuild

param(
    [string]$BuildDir = "build/windows-msvc-chaos",
    [string]$ConfigFile = "",
    [string]$Configuration = "Release",
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot

if (-not $ConfigFile) {
    $ConfigFile = "$ProjectRoot/runtime/validation/configs/soak-chaos.json"
}

# ── 读取配置文件 ────────────────────────────────────────────────────
if (-not (Test-Path $ConfigFile)) {
    Write-Host "[ERROR] Config file not found: $ConfigFile" -ForegroundColor Red
    exit 1
}

$Config = Get-Content $ConfigFile -Raw | ConvertFrom-Json

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Boost Gateway Chaos Soak Test Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Config          : $ConfigFile"
Write-Host "Build dir       : $BuildDir"
Write-Host "Configuration   : $Configuration"
Write-Host "Duration        : $($Config.duration_minutes) minutes"
Write-Host ""

# ── 构建 ────────────────────────────────────────────────────────────
if (-not $SkipBuild) {
    Write-Host "=== Configuring with chaos & security tests enabled ===" -ForegroundColor Green

    $ConfigResult = & cmake -B $BuildDir `
        -DBOOST_BUILD_CHAOS=ON `
        -DBOOST_BUILD_SECURITY_TESTS=ON `
        -DCMAKE_BUILD_TYPE=$Configuration `
        2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed:" -ForegroundColor Red
        Write-Host $ConfigResult
        exit 1
    }

    Write-Host "=== Building chaos and security test targets ===" -ForegroundColor Green

    $BuildTargets = @(
        "chaos_gateway_tests",
        "chaos_stability_tests",
        "security_tests"
    )

    foreach ($Target in $BuildTargets) {
        Write-Host "  Building $Target ..."
        $BuildResult = & cmake --build $BuildDir --config $Configuration --target $Target 2>&1
        if ($LASTEXITCODE -ne 0) {
            Write-Host "  [FAILED] $Target" -ForegroundColor Red
        } else {
            Write-Host "  [OK] $Target" -ForegroundColor Green
        }
    }
}

# ── 运行混沌规则描述 ────────────────────────────────────────────────
Write-Host ""
Write-Host "=== Chaos Rules ===" -ForegroundColor Yellow
foreach ($Rule in $Config.chaos_rules) {
    Write-Host "  - $($Rule.type): probability=$($Rule.probability), target=$($Rule.target)$(if ($Rule.max_delay_ms) { ", max_delay_ms=$($Rule.max_delay_ms)" })"
}

# ── 运行验证步骤 ────────────────────────────────────────────────────
$OverallStatus = $true
$Results = @()

foreach ($Step in $Config.validation_steps) {
    Write-Host ""
    Write-Host "=== Step: $($Step.name) ===" -ForegroundColor Magenta
    Write-Host "  Suite : $($Step.test_suite)"
    Write-Host "  Filter: $($Step.gtest_filter)"
    Write-Host "  Timeout: $($Step.timeout_seconds)s"

    # 查找测试二进制
    $TestBinary = "$ProjectRoot/$BuildDir/tests/chaos/$Configuration/$($Step.test_suite).exe"
    if (-not (Test-Path $TestBinary)) {
        $TestBinary = "$ProjectRoot/$BuildDir/tests/chaos/$($Step.test_suite).exe"
    }
    if (-not (Test-Path $TestBinary)) {
        $TestBinary = "$ProjectRoot/$BuildDir/tests/security/$Configuration/$($Step.test_suite).exe"
    }
    if (-not (Test-Path $TestBinary)) {
        $TestBinary = "$ProjectRoot/$BuildDir/tests/security/$($Step.test_suite).exe"
    }

    if (-not (Test-Path $TestBinary)) {
        Write-Host "  [SKIP] Binary not found: $($Step.test_suite)" -ForegroundColor Yellow
        continue
    }

    Write-Host "  Binary: $TestBinary"

    $StartTime = Get-Date
    $TestOutput = & $TestBinary --gtest_filter=$($Step.gtest_filter) 2>&1
    $ExitCode = $LASTEXITCODE
    $Elapsed = (Get-Date) - $StartTime

    $StepResult = @{
        Name = $Step.name
        Passed = ($ExitCode -eq 0)
        ExitCode = $ExitCode
        Duration = $Elapsed.TotalSeconds
    }
    $Results += $StepResult

    if ($ExitCode -eq 0) {
        Write-Host "  [PASSED] ($($Elapsed.TotalSeconds.ToString('F1'))s)" -ForegroundColor Green
    } else {
        Write-Host "  [FAILED] Exit code=$ExitCode ($($Elapsed.TotalSeconds.ToString('F1'))s)" -ForegroundColor Red
        $OverallStatus = $false
    }

    # 输出测试摘要
    $TestOutputStr = $TestOutput -join "`n"
    if ($TestOutputStr -match "\[==========\] .* tests from .* test suite") {
        $Summary = $TestOutputStr | Select-String -Pattern "\[=+\].*" | Select-Object -Last 1
        if ($Summary) {
            Write-Host "  GTest: $($Summary.Line)"
        }
    }
}

# ── 最终报告 ────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Chaos Soak Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan

$Passed = ($Results | Where-Object { $_.Passed }).Count
$Failed = ($Results | Where-Object { -not $_.Passed }).Count
$Total = $Results.Count

Write-Host "Total steps    : $Total"
Write-Host "Passed         : $Passed"
Write-Host "Failed         : $Failed"

foreach ($Result in $Results) {
    $Symbol = if ($Result.Passed) { "[PASS]" } else { "[FAIL]" }
    $Color = if ($Result.Passed) { "Green" } else { "Red" }
    Write-Host "  $Symbol $($Result.Name) ($($Result.Duration.ToString('F1'))s)" -ForegroundColor $Color
}

if ($OverallStatus) {
    Write-Host ""
    Write-Host "Chaos soak validation PASSED." -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "Chaos soak validation FAILED — check logs above for details." -ForegroundColor Red
    exit 1
}
