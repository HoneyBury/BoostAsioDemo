# ADR: v3 Proto / gRPC Transport

状态：Accepted for contract, Deferred for default transport  
日期：2026-05-18

## 背景

当前生产业务闭环已经稳定在 SDK / TCP gateway / BackendEnvelope / 五后端 / Redis 的路径。仓库内已有 v3 `.proto` schema、typed envelope helper、BackendEnvelope adapter、schema checker 和生成 helper，但默认生产传输不是 generated gRPC。

## 决策

短期内不把 generated gRPC 作为默认生产 transport。v3 proto 继续作为 schema/typed envelope 契约层和迁移准备层；默认生产仍使用 v2 TCP + BackendEnvelope。

原因：

- 当前 SDK full-flow、Docker production snapshot、性能基线、Redis/Raft HA、观测和控制面证据都围绕 v2 TCP + BackendEnvelope 收束。
- typed envelope helper 已覆盖 login/room/battle/match/leaderboard，能先冻结跨服务语义。
- 直接切 gRPC 会扩大性能、部署、TLS、负载均衡和客户端兼容面，收益需要独立性能对比证明。

## 已完成契约

- `proto/v3/*.proto`
- `include/v3/proto/envelope_codec.h`
- `include/v2/service/envelope_adapter.h`
- `scripts/check_v3_proto_schema.py`
- `check_v3_proto_schema`
- `check_v3_proto_transport_contract`
- `generate_v3_proto_cpp`
- `ServiceBusIntegrity` typed/proto envelope round-trip tests

## 验证

```bash
python3 scripts/check_v3_proto_schema.py --proto-dir proto/v3
python3 scripts/check_v3_proto_schema.py --proto-dir proto/v3 --require-transport-contract
cmake --build build/default --target check_v3_proto_schema check_v3_proto_transport_contract
python3 scripts/check_v3_grpc_poc_decision.py --build-dir build/default
```

P5-P8 聚合入口也会执行 schema 与 transport contract 检查：

```bash
python3 scripts/verify_p5_p8_business_closure.py --build-dir build/default --skip-build
```

## 进入 generated gRPC 的前置条件

- 独立 gRPC transport PoC，不替换默认 TCP 主链。
- SDK full-flow 在 gRPC profile 下通过。
- echo/battle/match/leaderboard/settlement 性能对比，含 P50/P90/P99、CPU、RSS、fd。
- TLS/mTLS、负载均衡、健康检查、deadline、重试、backpressure 和回滚策略明确。
- 发布文档中明确 raw JSON、typed envelope、generated gRPC 的兼容窗口。

## N6 判定条件

当前 N6 结论：继续保留实验，不进入默认生产链路。

可以继续推进到下一阶段的条件：

- `scripts/check_v3_grpc_poc_decision.py` 通过，且 summary 中仍明确默认链路是 SDK + TCP gateway + BackendEnvelope。
- 生成式 gRPC profile 具备独立服务进程或示例，不复用默认 gateway 端口伪装成功。
- gRPC profile 的 SDK full-flow 覆盖 login、room、battle、matchmaking、leaderboard、settlement 和 reconnect。
- 与 `runtime/perf/release-baseline/summary.json` 中 TCP baseline 对比后，P99、CPU/RSS、连接数和排障成本有明确收益或可接受代价。

必须暂停或回退的条件：

- 需要替换默认 TCP 主链才能运行 PoC。
- 无法给出 SDK full-flow 证据，只停留在 schema 或 stub 生成。
- 性能或资源成本显著劣于 TCP baseline，且没有 TLS、LB、可观测性或客户端生态收益抵消。
- 迁移要求客户端一次性切换协议，无法提供兼容窗口和灰度回滚。

## 迁移风险清单

| 风险 | 当前处理 |
| --- | --- |
| 默认主链被实验传输污染 | N6 gate 明确检查 current-state 和 ADR，要求默认链路仍是 TCP + BackendEnvelope |
| stub 生成被误认为生产可用 | ADR 和 `proto/README.md` 明确 generated stub 不是默认唯一传输路径 |
| 性能收益无证据 | N6 gate 需要 TCP baseline summary；gRPC benchmark 缺失时只能得出“实验保留” |
| TLS/LB/deadline/retry/backpressure 语义扩大 | 进入下一阶段前必须单独写 profile 和 full-flow 证据 |
| 客户端迁移风险 | 维持 SDK `4.x` TCP 兼容线，gRPC 只能作为独立 profile 灰度 |
