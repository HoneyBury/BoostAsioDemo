# Legacy / Helper Inventory

更新时间：2026-05-28

本文档记录当前仓库仍保留的 legacy 兼容面、helper 迁移层和默认主线之外的过渡入口。它不是未来规划文档，而是当前事实清单；未来规划仍以 `docs/project-blueprint.md` 为准。

## 作用范围

- 说明哪些兼容层仍然存在，为什么存在，以及它们是否属于默认生产主线。
- 约束新增功能不得继续扩张 legacy raw JSON 或 v1 example surface。
- 为 `scripts/check_legacy_helper_inventory.py` 提供可校验的事实源。

## 总体规则

1. 默认主线仍是 `SDK + TCP gateway + BackendEnvelope + typed envelope helper + 五后端 + Redis`。
2. `legacy raw JSON` 只允许作为兼容测试和迁移窗口保留，不得承载新增主功能。
3. `generated proto` / `generated protobuf / gRPC stub` 已经存在生成入口，但还不是默认唯一传输路径。
4. v1 example/showcase 只作为显式 legacy 兼容面保留，不再属于默认构建/安装面。
5. 新增 legacy/helper surface 时，必须同时更新本文档、相关测试和治理脚本。

## Helper 兼容层

| 项目 | 当前状态 | 默认主线角色 | 证据 |
| --- | --- | --- | --- |
| `BackendEnvelope` | 当前跨服务默认外层契约 | 默认主线的一部分 | `include/v2/service/backend_envelope.h`, `src/v2/gateway/gateway_service_bridge.cpp` |
| typed envelope helper | 已覆盖 `login/room/battle/match/leaderboard` | 默认主线的一部分 | `include/v2/service/envelope_adapter.h`, `src/v2/service/envelope_adapter.cpp`, `proto/README.md` |
| `legacy raw JSON` | 兼容窗口仍在，带 deprecation notice | compatibility-only，不得扩展 | `include/v2/service/envelope_adapter.h`, `tests/v2/unit/service_boundary_test.cpp` |
| generated proto | 已有 schema 和生成入口 | migration layer，不是默认 transport | `proto/v3/*.proto`, `scripts/generate_proto_cpp.py`, `proto/README.md` |
| generated protobuf / gRPC stub | 可生成，但仍属实验能力 | experimental_only | `proto/README.md`, `scripts/check_v3_grpc_poc_decision.py`, `src/v2/grpc/` |

## 服务级迁移状态

| 服务域 | 当前 handler 路径 | typed envelope | legacy raw JSON | generated proto 备注 |
| --- | --- | --- | --- | --- |
| login | `src/v2/login/login_backend_service.cpp` | 已接入 | compatibility-only | schema 已存在，未替换默认 transport |
| room | `src/v2/room/room_backend_service.cpp` | 已接入 | compatibility-only | schema 已存在，未替换默认 transport |
| battle | `src/v2/battle/battle_backend_service.cpp` | 已接入 | compatibility-only | schema 已存在，未替换默认 transport |
| matchmaking | `src/v2/matchmaking/matchmaking_service.cpp` | 已接入 | compatibility-only | schema 已存在，未替换默认 transport |
| leaderboard | `src/v2/leaderboard/leaderboard_service.cpp` | 已接入 | compatibility-only | schema 已存在，未替换默认 transport |

## Legacy 构建面

| 入口/模块 | 当前状态 | 默认状态 | 备注 |
| --- | --- | --- | --- |
| `project_game` / `include/game` / `src/game` | v1 风格单进程旧主链 | 仍被部分 legacy 与 integration 依赖 | 冻结，不新增能力 |
| `examples/echo` / `echo_server` | v1/v2 桥接入口 | 默认保留 | 当前 integration fixture 仍依赖 |
| `examples/login` / `room` / `battle` | v1 独立入口 | `BOOST_BUILD_V1_LEGACY_EXAMPLES=OFF` | legacy-v1 |
| `examples/pressure` / `gateway_pressure` | v1 压测入口 | `BOOST_BUILD_V1_LEGACY_EXAMPLES=OFF` | legacy-v1 |
| `examples/login_demo` / `room_demo` / `battle_demo` / `admin_demo` | showcase 入口 | `BOOST_BUILD_V1_LEGACY_EXAMPLES=OFF` | legacy showcase |
| `demo/games/tank_battle/` | 业务 demo | `BOOST_BUILD_TANK_DEMO=OFF` | 不属于默认生产主线 |
| `examples/realtime_echo_plugin` | demo/plugin 样例 | `BOOST_BUILD_ECHO_PLUGIN_DEMO=OFF` | 不属于默认生产主线 |

## 禁止新增的行为

- 不得新增仅支持 `legacy raw JSON` 的 handler 或 payload。
- 不得把 `login_server` / `room_server` / `battle_server` / `gateway_pressure` / `*_demo` 重新放回默认安装面。
- 不得把 demo 业务规则写回 `gateway`、公共 runtime、公共 SDK 或公共协议层。

## 进入默认主线前的条件

- helper 或 proto 迁移必须有对应的 schema、typed contract 测试和 full-flow 证据。
- legacy raw JSON 真正退场前，五个服务域都必须完成 generated/typed contract 覆盖。
- `echo_server` 退场前，旧 integration fixture 必须迁移到 `v2_gateway_demo` + v2 backend 组合。

## 治理入口

```bash
python scripts/check_legacy_helper_inventory.py
python scripts/check_mainline_readiness.py
python scripts/check_script_inventory.py
```
