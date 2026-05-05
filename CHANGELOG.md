# Changelog

## v1.0.0 (2026-05-05)

### Core Architecture
- Binary protocol: length-prefixed with message ID, request ID, error code, and flags byte
- Session: async TCP with heartbeat, rate limiting, max packet size check
- MessageDispatcher: handler registration, middleware chain, per-range thread pools
- SessionManager: authentication state, duplicate login handling, session migration
- RoomManager: create/join/leave/ready, owner mechanism, COW broadcast snapshot
- BattleManager: start/end, frame sync (advance_frame), input history, spectator support

### Business Services
- LoginService: dev/json_file/http auth providers, token TTL (24h), duplicate kick
- RoomService: room lifecycle, state broadcast, ready tracking
- BattleService: battle start, input routing, frame sync, settlement
- PushService: unified ok/error/push responses
- GatewayService: auth whitelist, rate limit middleware
- AdminService: kick_player, ban_ip, server_status, reload_config
- MatchmakingService: queue-based matching with ELO rating spread

### Observability
- GatewayMetrics: 10 counters (sessions, packets, bytes, blocks, logins, rooms, battles)
- GatewayMetricsExporter: Prometheus text + JSON snapshot with per-second rate gauges
- HttpManager: `/health`, `/metrics`, `/metrics/json` on configurable management port
- Request trace ID: generated at Session ingress, propagated through Dispatcher to handlers
- Audit logging: login_success/failure, rate_limited, connection_rejected, config_reload
- Crash handler: Windows SEH + POSIX signals, report to `runtime/crashes/`
- Log sampling: `LOG_INFO_SAMPLED(N, ...)` for high-frequency paths

### Performance
- BufferPool: reusable `std::string` and `std::vector<char>` pools
- ObjectPool<T>: generic pooled allocation
- Auto-compression: transparent zlib compress/decompress for >512B bodies
- Packet fragmentation: >8KB auto-split into 4KB fragments with reassembly
- Batch send: `Session::send_batch()` for single-write broadcast
- Zero-copy read: buffer pool integration in Session read path
- Broadcast lock optimization: COW snapshot via `broadcast_to_room()`
- Write queue backpressure: pause read at 75% watermark, resume at 25%
- Slow connection detection: WARN when write backlog exceeds 50% of limit

### Security
- Token lifecycle: `expires_at` field, 24h TTL (1h for dev tokens)
- Connection limits: max total + per-IP with rejection logging
- Rate limiting: per-connection (warm-up ramp), per-user, per-message-type
- Login protection: per-IP + per-user attempt tracking with auto-block
- Guest accounts: restricted permissions, reduced rate limit, max_guests cap
- TLS config: cert chain + private key, `asio::ssl::stream` integration
- Audit log: structured JSON security events

### Engineering
- CMake presets: windows-msvc-debug, default (ninja), release
- Third-party management: FetchContent + local `third_party/` for offline builds
- Docker: multi-stage build, `docker-compose.yml` with healthcheck
- CI: GitHub Actions build+test matrix (ubuntu/windows/macos) + Docker job
- 54 tests: 34 unit + 8 integration + 7 fuzz + 5 misc
- 8 pressure scenarios: echo, invalid_token, slow_echo, broadcast_storm,
  malicious_packet, battle_broadcast, chaos, stability
- 6 executables: echo_server, echo_client, gateway_pressure,
  login_server, room_server, battle_server

### Monitoring
- Grafana dashboard: 12 panels (sessions, rooms, battles, packets/s, bytes/s, health)
- Prometheus alerts: 7 rules (down, error rate, saturation, slow connections, auth spike,
  backlog, metrics absence)
- Per-second rate gauges: packets_recv/s, packets_sent/s, bytes_recv/s, bytes_sent/s,
  logins/s, sessions_accepted/s

### Protocol Messages
- System: 1-2 (heartbeat), 1001-1004 (echo, kicks), 9001 (error)
- Login: 2001-2002
- Room: 3001-3009 (create/join/leave/ready/push)
- Battle: 4001-4006 (start/input/push/state)
- Admin: 5001-5005 (kick/ban/status/reload/response)
