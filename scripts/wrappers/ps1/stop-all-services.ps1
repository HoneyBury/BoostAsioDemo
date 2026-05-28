<#
.SYNOPSIS
    Stop all Boost Gateway backend services in reverse dependency order.

.DESCRIPTION
    Reads runtime/processes.json (written by start-all-services.ps1) to
    discover running processes and stops them in reverse order: gateway,
    battle, room, matchmaking, leaderboard, login.

    Supports three modes:
    - Process mode: sends Ctrl+C (via Stop-Process), waits, then force kills.
    - Windows Service mode: stops services via Stop-Service.
    - Watchdog mode: stops the Watchdog parent process which handles
      child cleanup.

    After stopping all services, runtime/processes.json is cleaned up.

.PARAMETER Force
    Skip graceful shutdown and immediately terminate all processes.

.PARAMETER ProjectRoot
    Root directory of the Boost Gateway project. Auto-detected if omitted.

.EXAMPLE
    .\scripts\stop-all-services.ps1

.EXAMPLE
    .\scripts\stop-all-services.ps1 -Force
#>

param(
    [switch]$Force,
    [string]$ProjectRoot = ""
)

# ─── Auto-detect project root ──────────────────────────────────────
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
if (-not (Test-Path $ProjectRoot)) {
    Write-Error "Project root '$ProjectRoot' does not exist."
    exit 1
}

$ProcessesFile = Join-Path $ProjectRoot "runtime\processes.json"

# ─── Reverse dependency order ──────────────────────────────────────
$StopOrder = @(
    "gateway",
    "battle",
    "room",
    "matchmaking",
    "leaderboard",
    "login"
)

# ─── Helper: stop a process by PID ─────────────────────────────────
function Stop-ProcessGracefully {
    param(
        [int]$Pid,
        [string]$Name,
        [int]$Port,
        [string]$Type
    )

    if ($Type -eq "service") {
        # Windows Service mode.
        $serviceName = "BoostGateway_$Name"
        Write-Host "  Stopping Windows Service: $serviceName" -ForegroundColor Yellow
        try {
            Stop-Service -Name $serviceName -Force:$Force -ErrorAction Stop
            Write-Host "  Service $serviceName stopped." -ForegroundColor Green
        } catch {
            Write-Warning "  Failed to stop service $serviceName`: $_"
        }
        return
    }

    if ($Pid -le 0) {
        Write-Warning "  Invalid PID for $Name, skipping."
        return
    }

    Write-Host "  Stopping $Name (PID $Pid, port $Port)..." -ForegroundColor Yellow

    try {
        $proc = Get-Process -Id $Pid -ErrorAction SilentlyContinue
        if (-not $proc) {
            Write-Host "  $Name is not running." -ForegroundColor Green
            return
        }

        if ($Force) {
            $proc.Kill()
            Write-Host "  $Name force killed." -ForegroundColor Red
            return
        }

        # Graceful stop: send Ctrl+C equivalent via CloseMainWindow.
        if ($proc.CloseMainWindow()) {
            Write-Host "  Sent close signal to $Name, waiting..." -ForegroundColor Yellow
            # Wait up to 10 seconds for graceful exit.
            $waiting = $true
            $elapsed = 0
            while ($waiting -and $elapsed -lt 10) {
                $proc.Refresh()
                if ($proc.HasExited) {
                    $waiting = $false
                } else {
                    Start-Sleep -Seconds 1
                    $elapsed++
                }
            }

            if (-not $proc.HasExited) {
                Write-Warning "  $Name did not exit gracefully, force killing..."
                $proc.Kill()
            }
        } else {
            # CloseMainWindow failed (e.g., no window), use Kill.
            Write-Warning "  Could not send close signal to $Name (headless process), force killing..."
            $proc.Kill()
        }

        Write-Host "  $Name stopped." -ForegroundColor Green
    } catch {
        Write-Warning "  Error stopping $Name (PID $Pid): $_"
    }
}

# ─── Main ──────────────────────────────────────────────────────────
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Boost Gateway - Stop All Services"         -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host ""

if (-not (Test-Path $ProcessesFile)) {
    Write-Host "No processes.json found at $ProcessesFile" -ForegroundColor Yellow
    Write-Host "Attempting to discover running gateway processes..."

    # Fallback: try to stop known process names.
    $knownProcesses = @(
        "v2_gateway_demo",
        "v2_battle_backend",
        "v2_room_backend",
        "v2_match_backend",
        "v2_leaderboard_backend",
        "v2_login_backend"
    )

    $found = $false
    foreach ($name in $knownProcesses) {
        $procs = Get-Process -Name $name -ErrorAction SilentlyContinue
        if ($procs) {
            $found = $true
            foreach ($proc in $procs) {
                Write-Host "  Found $name (PID $($proc.Id))" -ForegroundColor Yellow
                if ($Force) {
                    $proc.Kill()
                } else {
                    $proc.CloseMainWindow()
                    Start-Sleep -Milliseconds 500
                    if (-not $proc.HasExited) {
                        $proc.Kill()
                    }
                }
                Write-Host "  Stopped $name (PID $($proc.Id))" -ForegroundColor Green
            }
        }
    }

    if (-not $found) {
        Write-Host "No running gateway services found." -ForegroundColor Green
    }
    exit 0
}

# Read processes.json.
try {
    $processData = Get-Content $ProcessesFile -Raw | ConvertFrom-Json
} catch {
    Write-Error "Failed to parse $ProcessesFile`: $_"
    exit 1
}

Write-Host "Mode: $($processData.mode)"
Write-Host ""

# Stop in reverse dependency order.
foreach ($serviceName in $StopOrder) {
    # Find matching process entry.
    $entry = $processData.processes | Where-Object { $_.Name -eq $serviceName }
    if (-not $entry) {
        Write-Host "  $serviceName : no entry found, skipping." -ForegroundColor DarkGray
        continue
    }

    Stop-ProcessGracefully -Pid $entry.PID -Name $entry.Name -Port $entry.Port -Type $entry.Type
}

# Cleanup runtime state.
Write-Host ""
Write-Host "Cleaning up runtime state..." -ForegroundColor Cyan
try {
    Remove-Item -Path $ProcessesFile -Force -ErrorAction SilentlyContinue
    Write-Host "Removed $ProcessesFile" -ForegroundColor Green
} catch {
    Write-Warning "Could not remove $ProcessesFile`: $_"
}

Write-Host ""
Write-Host "All services stopped." -ForegroundColor Green
exit 0
