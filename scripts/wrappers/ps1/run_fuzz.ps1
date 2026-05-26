# Boost Gateway — Fuzz 测试运行脚本（Windows）
#
# 运行方式：
#   .\scripts\run_fuzz.ps1 [-BuildDir <path>] [-Iterations <count>] [-TimeLimit <seconds>]
#
# 参数：
#   -BuildDir    构建输出目录（默认 build/fuzz-msvc）
#   -Iterations  每个 target 的最小迭代次数（默认 1000000）
#   -TimeLimit   每个 target 的最大运行时间，秒（默认 120）
#   -SkipBuild   跳过 cmake 构建步骤
#
# 示例：
#   .\scripts\run_fuzz.ps1 -Iterations 500000 -TimeLimit 60

param(
    [string]$BuildDir = "build/fuzz-msvc",
    [int]$Iterations = 1000000,
    [int]$TimeLimit = 120,
    [switch]$SkipBuild
)

$ErrorActionPreference = "Stop"

$ProjectRoot = Split-Path -Parent $PSScriptRoot
$FuzzTargets = @(
    "fuzz_protocol_codec",
    "fuzz_fragment_assembler"
)
$CrashDir = "$ProjectRoot/fuzz_crashes"

# 确保崩溃收集目录存在
New-Item -ItemType Directory -Path $CrashDir -Force | Out-Null

Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Boost Gateway Fuzz Test Runner" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Build directory : $BuildDir"
Write-Host "Iterations      : $Iterations"
Write-Host "Time limit      : ${TimeLimit}s"
Write-Host "Crash output    : $CrashDir"
Write-Host ""

if (-not $SkipBuild) {
    Write-Host "=== Configuring with BOOST_BUILD_FUZZ=ON ===" -ForegroundColor Green

    $ConfigResult = & cmake -B $BuildDir `
        -DBOOST_BUILD_FUZZ=ON `
        -DCMAKE_BUILD_TYPE=Release `
        -DCMAKE_CXX_FLAGS="/fsanitize=fuzzer /fsanitize=address /fsanitize=undefined" `
        2>&1

    if ($LASTEXITCODE -ne 0) {
        Write-Host "CMake configuration failed:" -ForegroundColor Red
        Write-Host $ConfigResult
        exit 1
    }

    Write-Host "=== Building fuzz targets ===" -ForegroundColor Green

    $BuildResult = & cmake --build $BuildDir --config Release 2>&1
    if ($LASTEXITCODE -ne 0) {
        Write-Host "Build failed:" -ForegroundColor Red
        Write-Host $BuildResult
        exit 1
    }
}

$GlobalStats = @{
    TotalCrashes = 0
    TotalRuns = 0
}

foreach ($Target in $FuzzTargets) {
    $FuzzBinary = "$ProjectRoot/$BuildDir/tests/fuzz/$Target.exe"
    $TargetCrashDir = "$CrashDir/$Target"

    if (-not (Test-Path $FuzzBinary)) {
        # 尝试查找构建输出（可能在 Release 子目录下）
        $FuzzBinary = "$ProjectRoot/$BuildDir/tests/fuzz/Release/$Target.exe"
        if (-not (Test-Path $FuzzBinary)) {
            Write-Host "[WARNING] Binary not found for target '$Target', skipping" -ForegroundColor Yellow
            # 尝试用 cmake --target 构建
            Write-Host "Attempting to build target $Target..." -ForegroundColor Yellow
            & cmake --build $BuildDir --config Release --target $Target 2>&1
            if (Test-Path $FuzzBinary) {
                Write-Host "Build succeeded." -ForegroundColor Green
            } else {
                continue
            }
        }
    }

    New-Item -ItemType Directory -Path $TargetCrashDir -Force | Out-Null

    Write-Host ""
    Write-Host "--- Running: $Target ---" -ForegroundColor Magenta
    Write-Host "  Binary  : $FuzzBinary"
    Write-Host "  Iterations: $Iterations"
    Write-Host "  Time    : ${TimeLimit}s"

    # 运行 fuzz target
    # libfuzzer 参数：
    #   -runs=N      运行 N 次后停止
    #   -max_total_time=N  最多运行 N 秒
    #   -artifact_prefix  崩溃文件前缀目录
    #   -jobs=N     并行 job 数
    #   -workers=N  并行 worker 数
    $StartTime = Get-Date
    $FuzzResult = & $FuzzBinary `
        -runs=$Iterations `
        -max_total_time=$TimeLimit `
        -artifact_prefix="$TargetCrashDir/" `
        2>&1

    $ExitCode = $LASTEXITCODE
    $Elapsed = (Get-Date) - $StartTime

    # 收集崩溃文件
    $NewCrashes = Get-ChildItem -Path $TargetCrashDir -Filter "crash-*" -ErrorAction SilentlyContinue
    if ($NewCrashes) {
        $GlobalStats.TotalCrashes += $NewCrashes.Count
        Write-Host "  [CRASHES] $($NewCrashes.Count) crash(es) found in $Target" -ForegroundColor Red
        foreach ($Crash in $NewCrashes) {
            Write-Host "    $($Crash.Name)" -ForegroundColor Red
        }
    }

    Write-Host "  Exit code: $ExitCode"
    Write-Host "  Elapsed  : $($Elapsed.TotalSeconds.ToString('F1'))s"

    # 打印 fuzz 输出摘要（最后 20 行）
    $FuzzOutput = $FuzzResult -join "`n"
    $Lines = $FuzzOutput -split "`n"
    if ($Lines.Count -gt 20) {
        $Lines = $Lines[-20..-1]
    }
    Write-Host "  Output tail:"
    foreach ($Line in $Lines) {
        Write-Host "    $Line"
    }

    $GlobalStats.TotalRuns++
}

# ── 报告摘要 ────────────────────────────────────────────────────────
Write-Host ""
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "  Fuzz Test Summary" -ForegroundColor Cyan
Write-Host "========================================" -ForegroundColor Cyan
Write-Host "Targets executed : $($GlobalStats.TotalRuns)/$($FuzzTargets.Count)"
Write-Host "Total crashes   : $($GlobalStats.TotalCrashes)"

if ($GlobalStats.TotalCrashes -gt 0) {
    Write-Host ""
    Write-Host "Crashes detected! Check $CrashDir for details." -ForegroundColor Red
    Write-Host "To reproduce a crash:" -ForegroundColor Yellow
    foreach ($Target in $FuzzTargets) {
        $TargetCrashDir = "$CrashDir/$Target"
        $Crashes = Get-ChildItem -Path $TargetCrashDir -Filter "crash-*" -ErrorAction SilentlyContinue
        foreach ($Crash in $Crashes) {
            $Binary = "$ProjectRoot/$BuildDir/tests/fuzz/$Target.exe"
            if (-not (Test-Path $Binary)) {
                $Binary = "$ProjectRoot/$BuildDir/tests/fuzz/Release/$Target.exe"
            }
            Write-Host "  $Binary $($Crash.FullName)" -ForegroundColor Yellow
        }
    }
    exit 1
} else {
    Write-Host "No crashes detected." -ForegroundColor Green
    Write-Host "All fuzz targets passed $Iterations iterations without issues."
}

exit 0
