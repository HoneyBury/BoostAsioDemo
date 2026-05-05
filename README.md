# Boost Game Server Framework v1.0.0

A high-performance C++20 game server framework built on Boost.Asio.

## Features

### Network Transport
- Binary protocol: `[4B len][2B msg][4B req][4B err][1B flags][body]`
- Auto-compression for large packets (>512B, `flags::kCompressed`)
- Packet fragmentation for >8KB payloads
- Batch send (`Session::send_batch`) for broadcast efficiency
- Zero-copy read path with buffer pool
- Write queue backpressure to prevent OOM
- TLS support (`TlsConfig` + `asio::ssl::stream`)

### Business Services
- **Login**: dev/json_file/http auth providers, token TTL, duplicate login handling
- **Room**: create/join/leave/ready, owner mechanism, room state broadcast
- **Battle**: start/input/frame sync/end, settlement, spectator mode
- **Matchmaking**: queue-based, ELO rating spread, configurable match size
- **Admin**: kick/ban/status/reload commands (msg 5001-5005)

### Observability
- Prometheus metrics (counters + per-second rate gauges)
- JSON metrics snapshot
- HTTP management endpoint: `/health`, `/metrics`, `/metrics/json`
- Grafana dashboard template (`grafana/dashboard.json`)
- Prometheus alerting rules (`prometheus/alerts.yml`)
- Request trace ID across all layers
- Security audit log (`logs/audit.log`, JSON lines)
- Crash dump handler (Windows SEH + POSIX signals)

### Operations
- Graceful shutdown (SIGINT/SIGTERM, save state, drain connections)
- Configuration hot-reload (file watcher)
- Connection limits (max total + per-IP)
- Rate limiting (per-connection warm-up + per-user + per-message-type)
- Guest account support
- Login brute-force protection

### Engineering
- CMake + FetchContent + local third_party (offline build)
- Docker + docker-compose + CI (GitHub Actions)
- 54 tests (unit + integration + fuzz)
- 8 pressure test scenarios
- Multi-process architecture (standalone login/room/battle servers)

## Quick Start

```powershell
# Build
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# Run gateway
./build/windows-msvc-debug/examples/echo/Debug/echo_server.exe config/gateway.json

# Health check
curl http://localhost:9080/health

# Pressure test
./build/windows-msvc-debug/examples/pressure/Debug/gateway_pressure.exe 127.0.0.1 9000 100 10 echo
```

## Module Architecture

```
include/
├── app/          config, logging, crash_handler, audit_log, graceful_shutdown
├── net/          session, packet_codec, message_dispatcher, buffer_pool,
│                 http_manager, rate_limiter, service_router, internal_bus
├── game/
│   ├── gateway/  gateway_server, session_manager, push_service, admin_service
│   ├── login/    login_service, token_validator, http_token_validator
│   ├── room/     room_manager, room_service
│   ├── battle/   battle_manager, battle_service, replay_player
│   ├── match/    matchmaking_service
│   └── persistence/ player_store, sqlite_store
src/               Implementation files
examples/          echo_server, echo_client, gateway_pressure,
                   login_server, room_server, battle_server
tests/             54 tests (unit + integration + fuzz)
```

## Configuration

See `config/gateway.json` for all options including TLS, auth providers,
connection limits, and session parameters.

## Third-Party Dependencies

Managed via CMake `FetchContent` or local `third_party/` archives:
- Boost 1.90.0 (Asio, Beast)
- fmt 11.2.0
- spdlog 1.15.3
- nlohmann/json 3.12.0
- GoogleTest 1.17.0

See `third_party/README.md` for offline build instructions.

## License

MIT
