# v2 启动清单与当前收口状态

> 本文档最初用于 `v2.0.0` 启动分批。
> 当前已重写为“启动批次回顾 + 当前主线状态”。

## 1. 启动结论

`v2.0.0` 启动阶段已经结束。

当前主线不是“准备开始 v2”，而是：

- `v2.0.0` 七大模块全部完成
- `v3.0.0-v3.3.x` 分布式与生产能力持续接入
- 当前重点转向验证链、typed transport、Operator status 和平台差异修复

## 2. 已完成的启动批次

| 批次 | 当前状态 | 说明 |
|---|---|---|
| `B0` 骨架初始化 | done | `v2/` 目录、CMake、tests/v2、v2 demo 入口 |
| `B1` Actor Runtime 原型 | done | `Actor` / `ActorRef` / `ActorSystem` / mailbox |
| `B2` Gateway Bridge | done | `SessionAdapter` / `GatewayActor` / bridge seam |
| `B3` PlayerActor | done | 登录态、顶号、恢复、房间/战斗归属 |
| `B4` RoomActor | done | 成员、owner、ready、battle start 条件 |
| `B4+` BattleActor shell | done | create/start/input/tick/end/settlement/finished |

## 3. 额外完成的 v2/v3 主线能力

已在启动批次之后进入主链的能力：

- `DemoServer` / `GatewayServiceBridge` 多后端路由
- `SchemaValidator`
- `InputValidator`
- `CachedBattleDataStore`
- `ClusterRouter`
- `OtlpExporter`
- `RedisLeaderboard`
- `RedisConnectionPool`
- typed `ServiceEnvelope` helper
- Operator controller scaffold

## 4. 当前收口项

当前重点收口项包括：

1. `ServiceEnvelope` typed helper 继续推进到 generated protobuf/gRPC
2. `login/room/battle/match/leaderboard` typed envelope 兼容层
3. 恢复/追平测试矩阵
4. Operator `Ready / Progressing / Degraded / TLSReady`
5. `kind` smoke 与 CI condition 断言

## 5. 当前不再适合作为“启动阶段待办”的内容

以下内容已不应继续放在“待启动”语境中：

- Actor 模型
- 多核 I/O
- 数据层 v2
- battle world
- FeatureFlags
- TraceContext / OtlpExporter
- ClusterRouter
- Redis persistence

## 6. 当前建议

如果需要继续推进主线，应直接围绕：

- CI 回归稳定性
- generated proto/gRPC
- 故障注入/恢复测试
- Operator rollout/dependency health

而不是回到早期 `v2` 启动骨架层面。
