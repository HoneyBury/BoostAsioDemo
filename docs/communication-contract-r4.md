# R4 通信契约与兼容迁移计划

> 日期：`2026-05-16`  
> 范围：typed envelope、generated proto/gRPC 路线、legacy raw JSON 兼容期、错误传播与性能基准入口。

## 1. 当前事实

当前项目处于过渡态：

| 层级 | 当前状态 |
|---|---|
| 客户端 TCP wire | 仍以 `message_id + request_id + error_code + body` 为主 |
| gateway 内部 typed 消息 | `v2::actor::MessagePayload` 已使用 typed variant |
| backend 服务间 envelope | `v2::service::BackendEnvelope` JSON 已冻结并有单测 |
| v3 typed envelope helper | `v3::proto::encode_typed_envelope()` / `decode_typed_envelope()` 已覆盖 login/room/battle/match/leaderboard |
| generated proto/gRPC | `.proto` schema 已存在，常规生成链和 gRPC transport 仍未接主链 |

## 2. 迁移原则

1. 先冻结 typed envelope 语义，再推进 generated proto。
2. legacy raw JSON payload 只保留在 gateway 边界和兼容路径。
3. 所有新 domain/message kind 必须有 domain 映射、round-trip 测试和错误传播测试。
4. gateway/backend/SDK 看到的 `correlation_id`、`trace_id`、`error_code` 必须一致。
5. 性能成本必须进入 R2/R1 类短基准或标准压测矩阵。

## 3. 兼容期策略

| 阶段 | 默认请求 | 默认响应 | 兼容要求 |
|---|---|---|---|
| R4-A | raw JSON 或 typed envelope 均接受 | request 是 envelope 时返回 envelope；否则返回 raw JSON | `maybe_wrap_typed_response()` 单测覆盖 |
| R4-B | typed envelope 优先 | typed envelope | raw JSON 标记 deprecated，继续解析 |
| R4-C | generated proto/gRPC 可选 transport | typed/proto 按配置切换 | CI 同时跑 JSON envelope 和 proto schema 测试 |
| R4-D | generated proto/gRPC 主路径 | proto/gRPC | raw JSON 只保留工具/迁移入口 |

## 4. 当前已固化测试

| 测试 | 覆盖 |
|---|---|
| `ProtoSchemaTest.AllProtoFilesExist` | proto 文件存在 |
| `ProtoSchemaTest.AllProtosUseProto3` | schema 使用 proto3 与统一 package |
| `ProtoSchemaTest.EnvelopeCodecRoundTripsMatchPayload` | envelope meta + payload round-trip |
| `ProtoSchemaTest.EnvelopeCodecSupportsLoginRoomAndBattleKinds` | login/room/battle typed helper |
| `ProtoSchemaTest.EveryTypedKindMapsToConcreteDomain` | 所有 typed kind 都能映射 domain 并 round-trip |
| `ProtoSchemaTest.MaybeWrapTypedResponsePreservesLegacyRawJsonCompatibility` | envelope/raw JSON 响应兼容策略 |

## 5. 下一步

- 增加 envelope/proto 编解码微基准，纳入 `v2_arch_benchmark` 或独立 `v3_contract_benchmark`。
- 为 `BackendEnvelope` 与 `v3::proto::TypedEnvelope` 建立显式转换层，而不是散落在业务 handler 中。
- 将 `proto` 生成 target 接入 CMake 可选目标，并在 CI 中验证 schema 不漂移。
- 在 gateway/backend integration test 中验证 `trace_id` / `error_code` 端到端一致。
