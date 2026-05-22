# Boost Gateway Architecture Overview

**Version**: 3.4.0

## High-Level Architecture

```
+-----------------------------------------------------------------------+
|                         Clients (SDK)                                 |
|  C++ SDK  |  Python (ctypes)  |  C# (P/Invoke)  |  gRPC (experimental)|
+-----------------------------+------------------------------------------+
                              | TCP (length-prefixed protocol)
                              v
+-----------------------------------------------------------------------+
|                      Gateway (v2_gateway_demo)                        |
|  +------------+  +------------+  +------------+  +------------------+  |
|  | Session    |  | Gateway    |  | Runtime    |  | HTTP Management |  |
|  | Manager    |  | Actor      |  | (actor)    |  | Port (health/   |  |
|  | (RCU)      |  |            |  |            |  |  metrics/audit) |  |
|  +------------+  +------------+  +------------+  +------------------+  |
|  +------------------------------------------------------------------+  |
|  |               GatewayServiceBridge                                |  |
|  |  Route requests | Circuit breaker | ClusterRouter | Shard        |  |
|  +------------------------------------------------------------------+  |
+-----------------------------------------------------------------------+
                              |
                +-------------+------------+------------+-----------+
                v             v            v            v           v
          +----------+ +--------+ +----------+ +-----------+ +----------+
          |  Login   | |  Room  | |  Battle  | |Matchmaking| |Leaderbrd |
          |  Backend | | Backend| |  Backend | | Backend   | | Backend  |
          +----------+ +--------+ +----------+ +-----------+ +----------+
                                                       |              |
                                                 +-----+-----+  +----+----+
                                                 | Raft      |  | Raft    |
                                                 | Consensus |  |Consensus|
                                                 +-----------+  +---------+
```

## Core Components

### Gateway Layer
- **SessionManager**: Lock-free RCU-based session tracking with backpressure (max_pending_per_session=1024)
- **GatewayActor**: Actor-model request handler, message dispatch, push delivery
- **Runtime**: Battle lifecycle management, match->battle auto-flow, session orchestration
- **GatewayServiceBridge**: Backend routing with circuit breaker, cluster discovery, shard-aware routing

### Backend Services
Each backend runs as an independent process with its own port:
- **Login** (:9101): Authentication, token management, rate limiting
- **Room** (:9102): Room CRUD, player join/leave, ready states, TTL cleanup
- **Battle** (:9103): Real-time game loop, ECS, anti-cheat, replay recording
- **Matchmaking** (:9104): MMR-based matching, Raft consensus for fault tolerance
- **Leaderboard** (:9105): Score submission/query, Raft consensus for consistency

### Actor System
- ActorSystem manages actor lifecycle and message dispatching
- Actors: GatewayActor, RoomBackend, BattleBackend, etc.
- Messages typed via `MessageKind` enum with `target_service` routing

### ECS (Entity Component System)
- World/SimpleWorld architecture with typed component access
- ParallelSystemExecutor with topological sort for system ordering
- Systems: MovementSystem, CombatSystem, LifecycleSystem

### Performance Features
- RCU (Read-Copy-Update) for lock-free broadcast in AOI and session management
- PerfCounters with TSC-based timing and TLS storage
- Cache-line aligned arena allocator
- Parallel system execution with dependency graph
- Per-session flow control and backpressure

### Persistence Layer
- WriteBehind queue with exponential backoff (100ms->5s, max 3 retries)
- SQLite storage engine with connection pool (min=2/max=8, WAL mode)
- CachedBattleDataStore for replay storage
- PlayerData LRU cache (1024 entries)

### SDK
- C API (src/sdk/src/c_api.cpp) as the stable ABI boundary
- Python bindings via ctypes
- C# bindings via P/Invoke
- Async API available via callback registration

### Consistency & HA
- **Raft consensus** for matchmaking and leaderboard state (3-node configs in config/environments/ha/)
- **Circuit breaker** pattern in gateway-to-backend routing
- **ClusterRouter** for dynamic service discovery and health checks (5s interval)
- **ServiceRegistrar** for automatic backend registration

## Data Flow: Request Routing

```
Client -> Gateway Session -> GatewayActor -> GatewayServiceBridge
    -> resolve_backend() [cluster router -> consistent hash -> static fallback]
    -> ensure_connection() [connection pool with round-robin]
    -> send_request() [with retry on failure]
    -> circuit breaker tracking
    -> response -> Client
```

## Data Flow: Matchmaking -> Battle

```
Client A -> MatchJoin
Client B -> MatchJoin
    -> Matchmaker (MMR-based pairing)
    -> MatchFoundCallback
    -> Runtime::on_match_found()
    -> create_room_with_players()
    -> send_push(kMatchFoundPush) to both clients
    -> wait for ready (or timeout)
    -> start_battle()
    -> redirect clients to Battle backend
```

## Deployment Models

### Single Process (Development)
```
v2_gateway_demo --io-cores 4 --port 9201
```
All backends are in-process actors. No Raft.

### Multi-Process (Production)
```
v2_gateway_demo --port 9201    (gateway)
v2_login_backend --port 9101
v2_room_backend --port 9102
v2_battle_backend --port 9103
v2_matchmaking_backend --port 9104 [--raft]
v2_leaderboard_backend --port 9105 [--raft]
```
Each process independent. Raft for stateful services.

### Kubernetes
```yaml
# See k8s/helm/gateway-server/ for full chart
# Operator: k8s/operator/operator.py (kopf-based)
# CRD: k8s/crds/gatewayservers.yaml
```

## Testing Strategy
| Level | Location | Scope |
|-------|----------|-------|
| Unit | tests/v2/unit/ | Individual components with mocks |
| Integration | tests/v2/integration/ | Multi-component E2E |
| Multi-process | tests/v2/integration/ (multi_process) | Real OS process orchestration |
| Chaos | tests/chaos/ | Network partition, crash, latency injection |
| Fuzz | tests/fuzz/ | Protocol codec fuzzing (libFuzzer) |
| Security | tests/security/ | Protocol-level attack simulation |
| Performance | scripts/collect_release_baseline.py | Throughput/latency regression detection |

## Key Protocols
- **Wire format**: `[length:4][version:1][message_id:2][request_id:4][sequence:4][error_code:4][flags:1][body:N]`
- **kFixedMetadataSize**: 16 bytes
- **Transport**: TCP (default), gRPC (experimental, behind BOOST_BUILD_GRPC flag)

## Related Documents
- [Service Discovery Guide](service-discovery-guide.md)
- [Performance Baseline](performance-baseline.md)
- [Anti-Cheat Baseline](anti-cheat-baseline.md)
- [K8s Deployment Guide](k8s-deployment-guide.md)
- [gRPC PoC Summary](grpc-poc-summary.md)
- [HA Deployment Guide](ha-deployment-guide.md)
