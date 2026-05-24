# Environment Variables Reference

**Version**: 3.4.0

## Core Runtime

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_BACKEND_CONNECTION_POOL_SIZE` | `1` | Max connections per backend in pool |
| `V2_GATEWAY_PORT` | `9201` | Gateway listen port |
| `V2_LOGIN_PORT` | `9101` | Login backend port |
| `V2_ROOM_PORT` | `9102` | Room backend port |
| `V2_BATTLE_PORT` | `9103` | Battle backend port |
| `V2_MATCHMAKING_PORT` | `9104` | Matchmaking backend port |
| `V2_LEADERBOARD_PORT` | `9105` | Leaderboard backend port |

## Backend Connection

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_LOGIN_HOST` | `127.0.0.1` | Login backend host |
| `V2_LOGIN_PORT` | `9101` | Login backend port |
| `V2_ROOM_HOST` | `127.0.0.1` | Room backend host |
| `V2_ROOM_PORT` | `9102` | Room backend port |
| `V2_BATTLE_HOST` | `127.0.0.1` | Battle backend host |
| `V2_BATTLE_PORT` | `9103` | Battle backend port |
| `V2_MATCHMAKING_HOST` | `127.0.0.1` | Matchmaking backend host |
| `V2_MATCHMAKING_PORT` | `9104` | Matchmaking backend port |
| `V2_LEADERBOARD_HOST` | `127.0.0.1` | Leaderboard backend host |
| `V2_LEADERBOARD_PORT` | `9105` | Leaderboard backend port |

## TLS / Security

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_TLS_CERT_FILE` | -- | Path to TLS certificate PEM |
| `V2_TLS_KEY_FILE` | -- | Path to TLS private key PEM |
| `V2_TLS_CA_FILE` | -- | Path to CA certificate PEM (mutual TLS) |
| `V2_TLS_ENABLED` | `false` | Enable TLS for backend connections |
| `V2_MTLS_REQUIRED` | `false` | Require mutual TLS |

## Performance & Debug

| Variable | Default | Description |
|----------|---------|-------------|
| `BOOST_GATEWAY_SDK_LIBRARY` | -- | Path to SDK shared library (Python SDK) |
| `V2_PERF_COUNTER_DUMP_SEC` | `60` | PerfCounter periodic dump interval |
| `V2_CIRCUIT_BREAKER_THRESHOLD` | `5` | Failure count to open circuit |
| `V2_CIRCUIT_BREAKER_TIMEOUT_MS` | `30000` | Circuit breaker recovery timeout |
| `V2_SESSION_MAX_PENDING` | `1024` | Max pending writes per session |

## Raft Consensus (HA deployment)

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_RAFT_NODE_ID` | -- | Unique node identifier (e.g., "node1") |
| `V2_RAFT_PEERS` | -- | JSON array of peer configs |
| `V2_RAFT_STORAGE_DIR` | `raft-data` | Raft log storage directory |
| `V2_RAFT_ELECTION_TIMEOUT_MS` | `150-300` | Election timeout range |

## Logging & Observability

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_LOG_LEVEL` | `info` | Log level (trace, debug, info, warn, error) |
| `V2_LOG_FILE` | -- | Log file path (stdout if unset) |
| `V2_AUDIT_LOG_PATH` | `logs/audit.log` | Audit log file path |
| `V2_AUDIT_LOG_MAX_SIZE_MB` | `50` | Max audit log size before rotation |
| `OTEL_EXPORTER_OTLP_ENDPOINT` | -- | OpenTelemetry OTLP exporter endpoint |

## CI / Testing

| Variable | Default | Description |
|----------|---------|-------------|
| `CI` | -- | Set by GitHub Actions |
| `BOOST_BUILD_GRPC` | `OFF` | Enable gRPC build targets |
| `BOOST_BUILD_FUZZ` | `OFF` | Enable fuzz test targets |
| `BOOST_BUILD_CHAOS` | `OFF` | Enable chaos test targets |
| `BOOST_BUILD_SECURITY_TESTS` | `OFF` | Enable security test targets |

## SDK (C API)

| Variable | Default | Description |
|----------|---------|-------------|
| `BOOST_GATEWAY_SDK_LIBRARY` | -- | Path to `boost_gateway_sdk` shared library |

## Windows Service

| Variable | Default | Description |
|----------|---------|-------------|
| `V2_SERVICE_NAME` | `BoostGateway` | Windows service name |
| `V2_SERVICE_DISPLAY_NAME` | `Boost Gateway Server` | Windows service display name |
| `V2_AUTO_RESTART` | `false` | Auto-restart on crash (ProcessSupervisor) |
| `V2_MAX_RESTARTS` | `5` | Max restart attempts before giving up |

## Kubernetes

| Variable | Default | Description |
|----------|---------|-------------|
| `KUBERNETES_SERVICE_HOST` | -- | Set by k8s for in-cluster API access |
| `KUBERNETES_SERVICE_PORT` | `443` | Set by k8s for in-cluster API access |
