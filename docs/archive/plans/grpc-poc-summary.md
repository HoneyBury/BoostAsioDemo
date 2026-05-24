# gRPC PoC Summary — Stage E: Protocol & Cross-Language

## Overview

This document summarizes the gRPC Proof of Concept implemented for Stage E of
the architecture roadmap. The PoC adds optional gRPC support alongside the
existing TCP-based custom protocol, providing a dual-stack architecture for
future cross-language client compatibility.

## Build

gRPC support is **opt-in** and **disabled by default**:

```bash
# Default build (no gRPC — unchanged from before)
cmake -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build --config Debug

# Build with gRPC support
cmake -B build -DCMAKE_BUILD_TYPE=Debug -DBOOST_BUILD_GRPC=ON
cmake --build build --config Debug
```

### Prerequisites

When `BOOST_BUILD_GRPC=ON`, the build requires:

- **protoc** (protobuf compiler) in PATH
- **grpc_cpp_plugin** (gRPC C++ plugin) in PATH
- **Protobuf** and **gRPC** CMake CONFIG packages installed

Recommended installation:
- **vcpkg**: `vcpkg install protobuf grpc`
- **Conan**: `conan install --requires=protobuf/3.x --requires=grpc/1.x`
- **Windows (manual)**: Install via vcpkg or build from source

## Architecture

```
                    ┌──────────────────────────────────────┐
                    │           Game Client                │
                    ├─────────────┬────────────────────────┤
                    │   TCP       │       gRPC             │
                    │ (custom     │      (HTTP/2)          │
                    │  protocol)  │                         │
                    └──────┬──────┴──────────┬─────────────┘
                           │                 │
                    ┌──────▼──────┐  ┌───────▼──────────┐
                    │  Existing   │  │   GatewayGrpc    │
                    │ TCP Server  │  │    Server         │
                    │ (port 9301) │  │  (port 50051)     │
                    └──────┬──────┘  └───────┬──────────┘
                           │                 │
                    ┌──────▼─────────────────▼──────────┐
                    │         GatewayActor               │
                    │     (shared business logic)        │
                    └───────────────────────────────────┘
```

### Component Map

| Component | Path | Role |
|-----------|------|------|
| Gateway gRPC Service Definition | `proto/v3/gateway.proto` | IDL: Login, Logout, Health RPCs |
| gRPC Server Base Class | `src/v2/grpc/grpc_server.h` | Lifecycle, error mapping |
| Gateway gRPC Server | `src/v2/grpc/gateway_grpc_server.h/.cpp` | Async service, CallData state machine |
| GrpcGatewayAdapter | `src/v2/grpc/grpc_adapter.h` | Dual-stack adapter (TCP ↔ gRPC) |
| Proto Generation | `proto/CMakeLists.txt` | CMake-native protobuf_generate / protobuf_generate_grpc |
| FindGRPC Module | `cmake/FindGRPC.cmake` | gRPC/protoc/plugin discovery |
| Perf Test | `tests/perf/grpc_vs_tcp_perf_test.cpp` | Latency/throughput comparison |
| Proto Schema | `proto/v3/*.proto` | All v3 proto files (6 existing + 1 new) |

## Error Code Mapping

Project error codes (`net::protocol::ErrorCode`) are mapped to gRPC status codes
via `ErrorCodeMapper` in `grpc_server.h`:

| Project Error | gRPC Status |
|---|---|
| kOk (0) | OK |
| kAuthRequired, kInvalidToken, kTokenExpired | UNAUTHENTICATED |
| kInvalidUserId, kInvalidRoomId | INVALID_ARGUMENT |
| kDuplicateLogin, kRoomAlreadyExists | ALREADY_EXISTS |
| kRoomNotFound, kSessionNotFound | NOT_FOUND |
| kRateLimited | RESOURCE_EXHAUSTED |
| Other / Unknown | UNKNOWN |

## File Inventory

### New Files Created

| # | File | Purpose |
|---|------|---------|
| 1 | `proto/v3/gateway.proto` | Gateway gRPC service IDL |
| 2 | `cmake/FindGRPC.cmake` | CMake module for gRPC discovery |
| 3 | `proto/CMakeLists.txt` | CMake-native proto/gRPC code generation |
| 4 | `src/v2/grpc/grpc_server.h` | gRPC server base class |
| 5 | `src/v2/grpc/gateway_grpc_server.h` | Gateway gRPC server header |
| 6 | `src/v2/grpc/gateway_grpc_server.cpp` | Gateway gRPC server impl |
| 7 | `src/v2/grpc/grpc_adapter.h` | Dual-stack adapter |
| 8 | `tests/perf/grpc_vs_tcp_perf_test.cpp` | Performance benchmark |
| 9 | `docs/grpc-poc-summary.md` | This document |

### Modified Files

| # | File | Change |
|---|------|--------|
| 10 | `CMakeLists.txt` (root) | Added `BOOST_BUILD_GRPC` option, proto subdirectory |
| 11 | `src/v2/CMakeLists.txt` | Added gRPC server source files (conditional) |
| 12 | `tests/CMakeLists.txt` | Added perf test subdirectory |
| 13 | `demo/games/tank_battle/server/tank_battle_main.cpp` | Added `--grpc` flag |
| 14 | `docs/architecture-roadmap.md` | Updated Stage E status |

## Performance Comparison (TBD)

*Results to be filled after running on target hardware.*

| Protocol | Concurrency | Avg Latency (us) | P99 (us) | Throughput (req/s) |
|----------|-------------|-------------------|----------|-------------------|
| TCP      | 1           | TBD               | TBD      | TBD               |
| TCP      | 100         | TBD               | TBD      | TBD               |
| TCP      | 10000       | TBD               | TBD      | TBD               |
| gRPC     | 1           | TBD               | TBD      | TBD               |
| gRPC     | 100         | TBD               | TBD      | TBD               |
| gRPC     | 10000       | TBD               | TBD      | TBD               |

## Known Limitations

1. **Completion Queue Threading**: The GrpcGatewayAdapter requires access to
   the GatewayGrpcServer's internal completion queue and service for seeding
   CallData objects. A production implementation should expose a
   `seed_completion_queue()` method on the server base class.

2. **Code Generation**: The `proto/CMakeLists.txt` uses `add_custom_command`
   for protoc invocation rather than CMake's `protobuf_generate` convenience
   function, because the latter's behavior with gRPC plugin paths varies
   across CMake versions.

3. **No Authentication**: The default login auth callback in the adapter
   accepts all requests. A production gateway would validate tokens against
   the login backend.

4. **Single Completion Queue**: The current implementation uses one CQ for
   all RPC types. For high-throughput scenarios, multiple CQs (one per CPU
   core) would reduce contention.

5. **TLS**: The current PoC uses `InsecureServerCredentials`. Production
   deployments should use `SslServerCredentials` with proper certificates.

6. **Performance Test**: The perf test file is a structural skeleton with
   simulated values. Real I/O against running TCP and gRPC servers is
   needed to produce meaningful data.

7. **Proto Imports**: The gateway.proto is self-contained and does not import
   other v3 proto files. Future services should reuse common types from
   `common.proto`.

## Next Steps

1. Install gRPC development libraries and verify `BOOST_BUILD_GRPC=ON` build.
2. Run the performance benchmark against real TCP and gRPC server instances.
3. Add TLS support with certificate configuration.
4. Extend gRPC support to Room and Leaderboard services.
5. Add cross-language client examples (Python, C#).
6. Migrate from `add_custom_command` to `protobuf_generate` CMake functions
   once CMake version support is confirmed.
