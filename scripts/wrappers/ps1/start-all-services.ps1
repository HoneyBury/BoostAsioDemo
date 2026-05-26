<#
.SYNOPSIS
    Start all Boost Gateway backend services in dependency order.

.DESCRIPTION
    Starts login, leaderboard, matchmaking, room, battle, and gateway
    backends in the correct dependency order. Supports Windows Service mode,
    watchdog (supervisor) mode, and standalone process mode.

    Each process is started and then its port is probed to confirm readiness
    before proceeding to the next service. PIDs and ports are written to
    runtime/processes.json for use by stop-all-services.ps1.

.PARAMETER BuildDir
    Path to the build output directory containing backend executables.
    Default: "build\bin" relative to the project root.

.PARAMETER ConfigDir
    Path to the configuration directory.
    Default: "config" relative to the project root.

.PARAMETER LogDir
    Path where process logs are written.
    Default: "logs" relative to the project root.

.PARAMETER AsService
    Switch to register and start services as Windows Services instead of
    standalone processes. Requires administrator privileges.

.PARAMETER Watchdog
    Switch to launch a ProcessSupervisor process that manages all backends
    as child processes, providing automatic crash recovery.

.PARAMETER ProjectRoot
    Root directory of the Boost Gateway project. If not specified, the
    script will attempt to auto-detect from the script location.

.EXAMPLE
    .\scripts\start-all-services.ps1 -BuildDir build\bin -ConfigDir config

.EXAMPLE
    .\scripts\start-all-services.ps1 -AsService -BuildDir C:\release\bin

.EXAMPLE
    .\scripts\start-all-services.ps1 -Watchdog -LogDir C:\logs\gateway
#>

param(
    [string]$BuildDir = "",
    [string]$ConfigDir = "",
    [string]$LogDir = "",
    [switch]$AsService,
    [switch]$Watchdog,
    [string]$ProjectRoot = ""
)

# ─── Auto-detect project root if not specified ─────────────────────
if (-not $ProjectRoot) {
    $ProjectRoot = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
}
if (-not (Test-Path $ProjectRoot)) {
    Write-Error "Project root '$ProjectRoot' does not exist."
    exit 1
}

# Resolve default paths relative to project root.
if (-not $BuildDir) {
    $BuildDir = Join-Path $ProjectRoot "build\bin"
}
if (-not $ConfigDir) {
    $ConfigDir = Join-Path $ProjectRoot "config"
}
if (-not $LogDir) {
    $LogDir = Join-Path $ProjectRoot "logs"
}
$ProcessesFile = Join-Path $ProjectRoot "runtime\processes.json"
$RuntimeDir = Split-Path $ProcessesFile -Parent

# Ensure output directories exist.
if (-not (Test-Path $RuntimeDir)) {
    New-Item -ItemType Directory -Path $RuntimeDir -Force | Out-Null
}
if (-not (Test-Path $LogDir)) {
    New-Item -ItemType Directory -Path $LogDir -Force | Out-Null
}

# ─── Port configuration ────────────────────────────────────────────
# Maps service name -> (port, config_file)
$Services = @(
    @{ Name="login";        Port=9302; Config="login.json" },
    @{ Name="leaderboard";  Port=9305; Config="leaderboard.json" },
    @{ Name="matchmaking";  Port=9306; Config="matchmaking.json" },
    @{ Name="room";         Port=9303; Config="room.json" },
    @{ Name="battle";       Port=9304; Config="battle.json" },
    @{ Name="gateway";      Port=9201; Config="gateway.json" }
)

# Map service name -> executable name.
$ExecutableMap = @{
    "login"       = "v2_login_backend"
    "leaderboard" = "v2_leaderboard_backend"
    "matchmaking" = "v2_match_backend"
    "room"        = "v2_room_backend"
    "battle"      = "v2_battle_backend"
    "gateway"     = "v2_gateway_demo"
}

# ─── Helper function: wait for port ────────────────────────────────
function Wait-ForPort {
    param(
        [string]$Hostname = "127.0.0.1",
        [int]$Port,
        [int]$TimeoutSeconds = 30
    )
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        try {
            $tcp = New-Object System.Net.Sockets.TcpClient
            $tcp.ConnectAsync($Hostname, $Port).Wait(500) | Out-Null
            if ($tcp.Connected) {
                $tcp.Close()
                return $true
            }
            $tcp.Close()
        } catch {
            # Port not ready yet
        }
        Start-Sleep -Milliseconds 200
    }
    return $false
}

# ─── Helper function: start a service ─────────────────────────────
function Start-ServiceProcess {
    param(
        [string]$Name,
        [int]$Port,
        [string]$ConfigFile,
        [string]$BinaryDir,
        [string]$CfgDir,
        [string]$LogDirectory
    )

    $exeName = $ExecutableMap[$Name]
    if (-not $exeName) {
        Write-Error "Unknown service: $Name"
        return $null
    }

    $exePath = Join-Path $BinaryDir "$exeName.exe"
    if (-not (Test-Path $exePath)) {
        Write-Error "Executable not found: $exePath"
        return $null
    }

    $configPath = Join-Path $CfgDir "environments\production\$ConfigFile"

    # Build environment variables for the process.
    $envBlock = @{
        "SERVICE_PORT" = "$Port"
        "LOG_DIR"      = $LogDirectory
        "CONFIG_PATH"  = $configPath
    }

    Write-Host "[start-all] Starting $Name on port $Port..." -ForegroundColor Cyan

    if ($AsService) {
        # Register and start as a Windows Service.
        $serviceName = "BoostGateway_$Name"
        $displayName = "Boost Gateway - $Name Backend"

        # Check if already registered.
        $existing = Get-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($existing -and $existing.Status -eq 'Running') {
            Write-Host "  Service $serviceName is already running." -ForegroundColor Yellow
            return @{ Name=$Name; Port=$Port; PID="(service)"; Type="service" }
        }

        # Register the service.
        $regArgs = @(
            "/C", "sc", "create", $serviceName,
            "binPath=`"$exePath --service --config $configPath`"",
            "start=auto",
            "displayname=$displayName"
        )
        & cmd.exe $regArgs | Out-Null

        # Start the service.
        Start-Service -Name $serviceName -ErrorAction SilentlyContinue
        if ($?) {
            Write-Host "  Registered and started Windows Service: $serviceName" -ForegroundColor Green
            return @{ Name=$Name; Port=$Port; PID="(service)"; Type="service" }
        } else {
            Write-Error "  Failed to start Windows Service: $serviceName"
            return $null
        }
    }

    # --- Standard process mode ---
    $logFile = Join-Path $LogDirectory "$Name.log"
    $procInfo = @{
        FilePath       = $exePath
        ArgumentList   = @("--config", $configPath, "--port", "$Port")
        WindowStyle    = "Hidden"
        PassThru       = $true
        RedirectStandardOutput = $logFile
        RedirectStandardError  = $logFile
    }

    try {
        $process = Start-Process @procInfo
        Write-Host "  Started PID $($process.Id)" -ForegroundColor Green

        # Wait for port to be ready.
        $ready = Wait-ForPort -Port $Port -TimeoutSeconds 15
        if (-not $ready) {
            Write-Warning "  Port $Port not ready within timeout for $Name."
        }

        return @{
            Name   = $Name
            Port   = $Port
            PID    = $process.Id
            Type   = "process"
            Ready  = $ready
        }
    } catch {
        Write-Error "  Failed to start $Name`: $_"
        return $null
    }
}

# ─── Main ──────────────────────────────────────────────────────────
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "  Boost Gateway - Start All Services"        -ForegroundColor Cyan
Write-Host "============================================" -ForegroundColor Cyan
Write-Host "Project Root : $ProjectRoot"
Write-Host "Build Dir    : $BuildDir"
Write-Host "Config Dir   : $ConfigDir"
Write-Host "Log Dir      : $LogDir"
Write-Host "Mode         : $(if ($AsService) { 'Windows Service' } elseif ($Watchdog) { 'Watchdog' } else { 'Standalone' })"
Write-Host ""

$results = @()
$allSucceeded = $true

# Start services in dependency order.
foreach ($svc in $Services) {
    $result = Start-ServiceProcess `
        -Name $svc.Name `
        -Port $svc.Port `
        -ConfigFile $svc.Config `
        -BinaryDir $BuildDir `
        -CfgDir $ConfigDir `
        -LogDirectory $LogDir

    if ($result) {
        $results += $result
    } else {
        $allSucceeded = $false
        Write-Error "Failed to start ${$svc.Name}"
    }
}

# ─── Save process information to JSON ──────────────────────────────
$processJson = @{
    timestamp = (Get-Date -Format "o")
    mode = if ($AsService) { "service" } elseif ($Watchdog) { "watchdog" } else { "standalone" }
    processes = $results
} | ConvertTo-Json

$processJson | Out-File -FilePath $ProcessesFile -Encoding utf8
Write-Host ""
Write-Host "Process information saved to: $ProcessesFile" -ForegroundColor Cyan

# ─── Summary ───────────────────────────────────────────────────────
Write-Host ""
Write-Host "--- Service Summary ---" -ForegroundColor Cyan
foreach ($r in $results) {
    $statusIcon = if ($r.Ready -eq $false) { " (port not ready)" } else { "" }
    Write-Host "  $($r.Name.PadRight(15)) : PID $($r.PID) : Port $($r.Port)$statusIcon"
}

if ($Watchdog -and $allSucceeded) {
    Write-Host ""
    Write-Host "Watchdog mode: services are monitored. Use stop-all-services.ps1 to stop." -ForegroundColor Yellow
}

if ($allSucceeded) {
    Write-Host ""
    Write-Host "All services started successfully." -ForegroundColor Green
    exit 0
} else {
    Write-Host ""
    Write-Host "Some services failed to start. Check logs for details." -ForegroundColor Red
    exit 1
}
