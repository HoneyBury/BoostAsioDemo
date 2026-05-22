# 实时系统框架实施改造计划

更新时间：2026-05-22

本文档定义主线从“游戏服务器框架”继续收敛为“企业级高性能实时服务框架”的实施计划。坦克大战、多游戏 demo、高性能网关中间件或其他实时网络系统只能作为验证样例接入，不能改变框架底座的目标、命名边界和发布门槛。

## 1. 总目标

主线目标保持不变：提供可复用、可部署、可观测、可压测、可恢复的实时网络服务底座。

框架必须稳定回答：

| 问题 | 框架责任 |
|---|---|
| 连接如何接入 | gateway、Session、TLS、限流、背压、心跳 |
| 消息如何流转 | wire protocol、typed envelope、request/response、push、schema validation |
| 服务如何拆分 | gateway-only ingress、backend route、service registry、health/readiness |
| 状态如何管理 | Actor/runtime、room/lobby、realtime instance、snapshot/replay hook |
| 可靠性如何证明 | release gate、soak、capacity、data recovery、Redis/Raft/Operator/TLS profile |
| 客户端如何接入 | SDK、C ABI、Python/C# wrapper、full-flow、错误与重连语义 |

业务只能回答具体领域规则：

| 问题 | 业务责任 |
|---|---|
| 实体如何移动 | 业务 simulation |
| 碰撞如何判定 | 业务 physics / rule system |
| 胜负如何计算 | 业务 settlement rule |
| 得分如何转榜单 | 业务 result adapter -> framework leaderboard |
| 客户端如何表现 | demo client / game client |

## 2. 现状判断

当前主线已经具备企业级实时服务框架的主要基础：

- `gateway + login/room/battle/matchmaking/leaderboard backend` 多进程链路。
- `BackendEnvelope`、typed envelope helper、协议 schema 校验和 gateway-only ingress。
- SDK C++ / C ABI / Python / C# 封装，覆盖 full-flow、heartbeat、push、reconnect。
- Redis leaderboard、Raft/Operator/TLS/OTel/K8s 等可选 profile 和专项 gate。
- Release baseline、capacity、business-flow、production evidence、readiness report 等证据链。

当前风险是业务 demo 容易继续把特定游戏规则写进 `battle`、SDK 或 gateway。后续必须通过目录、协议、文档和验收门禁约束：

- `login` 保持身份域，不承载具体游戏初始化奖励。
- `room` 保持 lobby/session 编排域，不承载具体游戏地图或玩法参数解释。
- `battle` 逐步抽象为 `realtime instance` 运行时，不承载具体坦克、子弹、碰撞、胜负规则。
- SDK 保持框架客户端 SDK，不暴露 `tank_fire()` 这类业务方法。
- demo 放入 `demo/games/`，作为验证样例，不进入框架默认发布面。

## 3. 目标架构

建议长期架构分为四层：

| 层级 | 目录建议 | 责任 |
|---|---|---|
| Core Runtime | `include/v2/runtime`、`include/v2/actor`、`include/v2/io`、`include/v3` | Actor、I/O、多核、service route、persistence、tracing、cluster |
| Framework Domains | `login`、`room`、`match`、`leaderboard`、`realtime_instance` | 身份、大厅、匹配、排行、通用实时实例生命周期 |
| SDK / Integration | `sdk/`、`examples/v2_*`、`scripts/verify_*` | 客户端接入、分发、full-flow、生产验证入口 |
| Demo / Business | `demo/games/*` | 独立业务样例，依赖框架，不反向污染框架 |

`battle` 的短期保留策略：

- 现有 `battle backend` 可以继续作为兼容入口。
- 新文档和新增接口应优先使用 `realtime instance` 语义。
- 新业务 demo 不应直接修改框架 `battle` 规则，而应通过业务插件或 demo backend 适配。

## 4. 实施阶段

### M0：边界固化

目标：先用文档和 review gate 防止业务规则进入框架。

任务：

- 新增 `docs/realtime-framework-module-boundaries.md`，定义每个框架模块允许和禁止承载的内容。
- 新增 `docs/realtime-framework-sdk-boundary.md`，定义 SDK 只能暴露通用实时系统 API。
- 新增 `demo/games/README.md`，规定所有游戏和实时系统样例的接入规则。
- 在 `docs/README.md` 索引中加入上述文档。

验收标准：

- 新 demo 需求可以根据文档判断落点。
- 代码 review 能直接引用边界文档阻断业务污染。
- 框架文档不再把坦克大战作为主线功能描述。

### M1：通用身份域扩展

目标：把“注册”收敛为通用 identity/auth 能力。

框架应提供：

- `register_account`
- `login`
- `token_refresh`
- `guest_login` 或外部身份接入 adapter
- session resume metadata
- 账号状态、封禁状态和基础 profile

禁止放入 identity/auth 的内容：

- 坦克初始皮肤、初始道具、阵营。
- 新手引导进度。
- 游戏段位初始化。
- 具体游戏奖励。

验收标准：

- 注册接口可以服务任意实时系统 demo。
- 登录后只返回通用账号和 session 信息。
- 业务 profile 由业务 demo 自己维护或通过独立业务数据服务维护。

### M2：通用 Lobby / Room 域扩展

目标：让 `room` 成为可复用实时会话编排能力。

框架应补齐：

- `list_rooms(filter, page)`
- `room_detail(room_id)`
- `create/join/leave`
- `ready/unready`
- `kick_member`
- `transfer_owner`
- room capacity、visibility、metadata
- room state push

room metadata 只能是框架可理解的通用 key/value 或 schema-less payload；具体业务解释由 demo 完成。

验收标准：

- 坦克大战、实时协作、推送压测 demo 都可以复用 room。
- room 不依赖任何 `Tank*`、`Bullet*`、`Map*` 类型。
- list/detail 能进入 SDK full-flow 和 gateway metrics。

### M3：Realtime Instance Runtime

目标：把现有 `battle` 逐步抽象为通用实时实例运行时。

框架应提供：

- instance create / attach / detach / destroy
- tick driver
- input queue
- snapshot push
- result event
- reconnect resume snapshot
- replay hook
- settlement hook
- backpressure and drop policy

业务插件负责：

- input payload 解释。
- world state 演进。
- collision、physics、AI、scoring。
- settlement payload 生成。

验收标准：

- 可以在同一 runtime 上接入 `tank_battle`、`echo_realtime`、`collaboration_presence` 等不同 demo。
- 框架 runtime 测试不依赖具体游戏规则。
- 业务 plugin 出错时能通过框架 metrics、trace、audit 定位。

### M4：业务插件 SPI

目标：用明确接口隔离框架和业务。

建议 SPI：

```cpp
struct RealtimeBusinessPlugin {
    virtual void on_instance_created(const InstanceContext&) = 0;
    virtual void on_player_join(const PlayerContext&) = 0;
    virtual void on_player_leave(const PlayerContext&) = 0;
    virtual InputResult on_input(const PlayerContext&, InputEnvelope) = 0;
    virtual TickResult on_tick(FrameContext) = 0;
    virtual Snapshot build_snapshot(const SnapshotContext&) = 0;
    virtual Settlement build_settlement(const SettlementContext&) = 0;
    virtual ResumePayload build_resume_payload(const ResumeContext&) = 0;
};
```

接口命名可按现有 C++ 风格调整，但职责必须保持：

- 框架控制生命周期和调度。
- 业务只实现规则和 payload。
- 框架只消费通用结果，不感知业务实体。

验收标准：

- 新 demo 不需要修改 gateway。
- 新 demo 不需要修改 SDK 公共 API。
- 新 demo 可以独立测试 simulation。

### M5：Demo Gate 与生产证据隔离

目标：demo 可以验证框架能力，但不能成为默认生产发布负担。

规则：

- demo gate 默认不阻断框架 release candidate，除非显式加入某个验证 profile。
- demo 失败不能影响 SDK distribution、transport governance、production evidence 默认结论。
- demo 可以有自己的 `verify_demo_*` 脚本和 runtime summary。
- demo 的性能数据只能证明“该 demo 在某环境下可运行”，不能替代框架 baseline。

验收标准：

- `verify_release_candidate.py` 保持框架默认有界 gate。
- `verify_tank_battle_demo.py` 等脚本独立存在。
- demo 报告明确引用框架版本、SDK 版本和运行 profile。

## 5. 模块归属矩阵

| 能力 | 归属 | 说明 |
|---|---|---|
| TCP/TLS 连接 | 框架 | gateway / net / transport |
| 心跳/断线检测 | 框架 | SDK 与 gateway 通用能力 |
| 注册/登录/token | 框架 | identity/auth 域 |
| 创建/加入/列出房间 | 框架 | lobby/room 通用域 |
| 准备/取消准备 | 框架 | 通用 staging 状态 |
| 开始实时实例 | 框架 | instance lifecycle |
| 输入投递 | 框架 + 业务 | 框架投递，业务解释 payload |
| tick 调度 | 框架 | 时间与调度属于 runtime |
| 碰撞检测 | 业务 | 只属于具体 simulation |
| 子弹/坦克/地图 | 业务 | 不进入框架公共模型 |
| 通用排行榜 | 框架 | leaderboard service |
| 坦克计分规则 | 业务 | 输出通用 score/result |
| 结算幂等提交 | 框架 | 通用 settlement sink |
| 结算内容生成 | 业务 | 业务规则决定 |
| metrics/tracing/audit | 框架 | 可附带业务标签，但不理解业务规则 |
| SDK `send_input` | 框架 SDK | 通用 API |
| SDK `tank_fire` | demo SDK adapter | 不进入框架 SDK |

## 6. 文档拆分

新增和维护以下事实源：

| 文档 | 责任 |
|---|---|
| `docs/realtime-framework-modernization-plan.md` | 总体实施路线 |
| `docs/realtime-framework-module-boundaries.md` | 框架与业务边界 |
| `docs/realtime-framework-sdk-boundary.md` | SDK 封装边界 |
| `demo/games/README.md` | demo 接入规范 |
| `demo/games/tank-battle-demo-plan.md` | 坦克大战 demo 规划 |
| `docs/current-state.md` | 当前主线事实源，必要时引用上述文档 |

## 7. 发布与验收原则

所有新增框架能力必须满足：

- 有通用命名，不绑定具体业务。
- 有协议或接口说明。
- 有单元测试或集成测试。
- 有 SDK 接入说明或明确不进入 SDK 的理由。
- 有 metrics / error / timeout / recovery 行为。
- 有 release gate 或独立验证入口。

所有新增 demo 必须满足：

- 目录在 `demo/games/<demo_name>/`。
- 不修改框架协议语义来适配单一业务。
- 业务协议、规则、资源、客户端说明自成文档。
- demo gate 独立于默认生产 gate。
- demo 结论不能替代框架生产 readiness 结论。

