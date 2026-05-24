# 服务端框架与坦克大战 Demo 开发计划

更新时间：2026-05-22

本文档是近期服务端实施计划。目标是先完成一个稳定、可维护、高性能的企业级后端实时网络服务框架，并在该框架上实现坦克大战 demo 服务端，用于验证框架承载具体业务的能力。当前阶段不实现正式客户端，不把坦克大战业务规则写入框架主线。

## 1. 目标与非目标

近期最终目标：

- 服务端框架具备通用 identity、lobby/room、matchmaking、realtime instance、leaderboard、observability、deployment gate 能力。
- 坦克大战 demo 服务端位于 `demo/games/tank_battle/`，通过框架通用能力接入。
- demo 能跑通注册/登录、列房、建房、进房、准备、开局、实时输入、服务端模拟、断线恢复、结算、排行榜。
- 所有新增能力有测试、文档、验收标准和回归门禁。

明确非目标：

- 不实现正式图形客户端。
- 不把 `tank_*` API 加入公共 SDK。
- 不把坦克碰撞、地图、子弹、胜负规则写进 gateway、login、room、leaderboard。
- 不让 tank demo gate 成为默认生产发布阻断项。
- 不用 demo 性能结论替代框架 release baseline。

## 2. 优先级总览

| 优先级 | 阶段 | 目标 | 建议周期 |
|---|---|---|---|
| P0 | 代码边界与目录落地 | 建立服务端 demo 隔离结构和评审边界 | 2-3 天 |
| P1 | Identity 服务端通用注册 | 补通用注册和账号状态，不引入业务 profile | 4-6 天 |
| P2 | Lobby / Room 服务端补齐 | 补列房、详情、房主操作和 metadata | 5-7 天 |
| P3 | Realtime Instance Runtime | 抽象通用实时实例生命周期、输入、tick、snapshot、resume | 8-12 天 |
| P4 | Tank Demo Simulation | 实现坦克服务端 simulation 和 plugin adapter | 8-12 天 |
| P5 | Settlement / Leaderboard | 完成 demo 结算到通用排行榜的幂等链路 | 4-6 天 |
| P6 | Reliability / Recovery | 补断线重连、snapshot/replay、故障恢复验证 | 6-8 天 |
| P7 | Performance / Regression | 压测、回归门禁、文档收束 | 5-7 天 |

总体建议周期：6-8 周。若只做最小可运行闭环，可压缩为 3-4 周，但 P6/P7 不能省略，只能降低压测规模。

## 3. 开发原则

- 每个阶段先补框架通用能力，再接 demo。
- 框架目录只能出现通用命名：`identity`、`room`、`realtime_instance`、`leaderboard`。
- demo 目录可以出现业务命名：`tank`、`bullet`、`map`、`collision`、`damage`。
- 新协议先判断是否属于框架 envelope；业务 payload 只能作为 opaque payload 穿过框架。
- 每个阶段必须有单元测试和至少一个服务端集成验证。
- 默认 release gate 不能因 demo 失败而失败；demo gate 使用显式脚本运行。

## 4. 计划阶段

### P0：代码边界与目录落地

目标：让服务端工程结构先具备业务隔离能力。

任务：

- 创建 `demo/games/tank_battle/` 服务端目录骨架。
- 新增 `README.md`、`docs/rules.md`、`docs/protocol.md`、`docs/acceptance.md`。
- 新增 `server/`、`server/tank_simulation/`、`server/tank_plugin/`、`tests/`、`scripts/`。
- 增加 CMake 可选入口，但默认构建不强制依赖 demo。
- 明确 demo gate 脚本命名：`demo/games/tank_battle/scripts/verify_tank_battle_demo.py`。

交付物：

- `demo/games/tank_battle/README.md`
- `demo/games/tank_battle/docs/*.md`
- demo CMake option，例如 `BOOST_BUILD_TANK_DEMO`

验收标准：

- 默认 `cmake --build --preset default` 不因 demo 缺失资源失败。
- demo 目录可以独立被搜索和构建。
- `rg -n "Tank|Bullet|Collision" include src sdk` 不应出现新增框架污染；允许出现在 `demo/games/tank_battle/`。

测试：

- 文档链接检查。
- CMake configure smoke。
- 默认测试集不增加业务 demo 依赖。

### P1：Identity 服务端通用注册

目标：把注册做成通用身份能力，为所有实时系统 demo 复用。

任务：

- 在 login backend 增加通用 `register_account` handler。
- 定义账号字段：`user_id`、`credential_hash` 或 dev credential、`display_name`、`status`、`created_at`。
- 定义错误码：用户已存在、非法用户名、弱凭证、账号禁用、存储不可用。
- 增加生产模式边界：dev token fallback 仍不能进入 production auth。
- 为注册结果增加审计事件和 metrics。
- SDK 公共 API 可先不实现；服务端测试可直接走协议或 backend service。

禁止：

- 注册时创建坦克角色。
- 注册时写入 demo 道具、皮肤、段位。

交付物：

- login backend 注册协议说明。
- 注册 handler 和账号存储抽象。
- 注册单测、backend 集成测试。

验收标准：

- 重复注册返回稳定错误。
- 注册后可以登录。
- 生产模式未配置安全凭证时拒绝不安全 fallback。
- 注册 audit 至少包含 action、user_id、result、error_code。

测试：

- `project_v2_unit_tests` 增加 identity register 用例。
- login backend integration：register -> login。
- security gate 不回归：`python3 scripts/check_security_release_gate.py`。

### P2：Lobby / Room 服务端补齐

目标：让 room 成为通用 lobby 编排服务，支持坦克 demo 和其他实时系统。

任务：

- 增加 `list_rooms(filter, page, page_size)`。
- 增加 `room_detail(room_id)`。
- 为 `create_room` 支持通用 metadata：mode、visibility、capacity、business_payload。
- 增加 `kick_member`、`transfer_owner`。
- 补 room state push 的版本字段和成员状态。
- 增加 room capacity、room in instance、closed room 等错误语义。
- gateway/backend metrics 覆盖 list/detail/kick/transfer。

禁止：

- room 层解析 `map_id` 的业务含义。
- room 层判断坦克队伍胜负。

交付物：

- room protocol 文档更新。
- RoomBackendService handler。
- RoomManager / RoomActor 通用状态扩展。
- room list/detail/kick/transfer 测试。

验收标准：

- 创建 1000 个 room 后分页列房稳定。
- room metadata 可以透传，但框架不解释业务字段。
- 房主离开后 owner transfer 规则确定。
- 房间进入实时实例后 join/ready/kick 行为清晰。

测试：

- room unit tests：metadata、pagination、owner transfer、capacity。
- gateway integration：login -> create -> list -> detail -> join -> ready。
- 回归：现有 create/join/leave/ready full-flow 不变。

### P3：Realtime Instance Runtime

目标：把当前 battle 兼容能力抽象为通用实时实例服务端运行时。

任务：

- 定义 `RealtimeInstanceService` 或兼容 `BattleBackendService` 的通用 facade。
- 定义 instance 状态：creating、waiting_players、running、finishing、finished、closed。
- 定义通用 input envelope：`instance_id`、`user_id`、`seq`、`payload_type`、`payload`、`client_time`。
- 定义 tick driver、input ordering、per-player input ack。
- 定义 snapshot push：full snapshot、delta snapshot、resume snapshot。
- 定义 backpressure：input queue 上限、snapshot 降频、慢客户端策略。
- 定义 instance resume window：默认 30 秒，可配置。
- 兼容现有 battle start/input/full-flow，避免已有 SDK 流程回归。

禁止：

- runtime 解释 `move/fire`。
- runtime 内置碰撞或得分。

交付物：

- `include/v2/realtime/` 或等价通用接口。
- 通用 instance runtime 单测。
- gateway route 到 instance backend 的集成测试。
- 现有 battle API 兼容说明。

验收标准：

- `echo_realtime` 或 fake plugin 可以在 runtime 上运行。
- 同一进程可同时运行多个 instance。
- 单个 instance 内输入有序，跨 instance 不互相阻塞。
- 断线后 resume window 内可获取 resume snapshot。
- 现有 `sdk_full_flow_client` 仍通过。

测试：

- runtime unit：状态机、input ordering、tick、snapshot、resume。
- integration：两玩家创建 instance、发送 opaque input、收到 snapshot。
- regression：`python3 scripts/verify_sdk_full_flow_client.py --build-dir build/default --skip-build`。
- performance smoke：小规模 instance input 压测。

### P4：Tank Demo 服务端 Simulation

目标：实现坦克大战服务端业务规则，但只放在 demo 目录。

任务：

- 定义 deterministic world：地图、墙、出生点、坦克、子弹。
- 实现固定 tick，例如 20Hz 或 30Hz。
- 实现输入：move、turn、fire、stop、finish_debug。
- 实现碰撞：坦克与墙、子弹与墙、子弹与坦克。
- 实现生命值、死亡、简单复活或局内结束。
- 实现基础反作弊：移速、射速、非法方向、重复 seq。
- 实现 snapshot：坦克状态、子弹状态、比分、剩余时间。
- 通过 `tank_plugin` 适配 P3 runtime SPI。

交付物：

- `demo/games/tank_battle/server/tank_simulation/`
- `demo/games/tank_battle/server/tank_plugin/`
- `demo/games/tank_battle/docs/rules.md`
- `demo/games/tank_battle/docs/protocol.md`

验收标准：

- 相同输入序列产生相同 world state。
- 非法移动不能穿墙。
- fire cooldown 生效。
- 子弹命中能扣血并产生事件。
- simulation 不依赖 gateway、Session、SDK。

测试：

- deterministic unit tests。
- collision tests。
- anti-cheat tests。
- plugin integration：runtime -> tank input -> snapshot。

### P5：Settlement / Leaderboard

目标：完成 tank demo 结算到框架 leaderboard 的幂等服务端链路。

任务：

- 定义 tank settlement payload：kills、deaths、damage、win、score、reason。
- 将业务 settlement 转换为框架 leaderboard submit。
- 使用 `idempotency_key = tank_battle:<battle_id>:<user_id>`。
- 增加 settlement retry / failure metrics。
- 增加 leaderboard unavailable 时的错误可观测性。
- demo 可查询 top/rank，但排行榜不解释坦克得分公式。

交付物：

- settlement adapter。
- tank scoring rule 文档。
- settlement -> leaderboard integration tests。

验收标准：

- 战斗结束后无需客户端手动 submit 即可查到 rank。
- 重复 settlement 不重复加分。
- leaderboard backend down 时有 metrics/error/audit。
- 得分公式只存在于 tank demo。

测试：

- settlement unit tests。
- idempotency tests。
- integration：tank battle finish -> leaderboard rank。
- failure test：leaderboard unavailable。

### P6：断线重连与恢复

目标：让服务端具备真实业务 demo 的恢复能力。

任务：

- 定义 session resume metadata。
- instance runtime 保存最近 resume snapshot。
- demo plugin 生成 tank resume payload。
- 断线期间策略：保留、静止、AI 托管或超时判负，先采用“保留并静止”。
- reconnect window 超时后清理玩家或结算。
- 增加恢复 metrics：resume success/failure/window expired。

交付物：

- resume protocol 文档。
- runtime resume tests。
- tank resume integration tests。

验收标准：

- 战斗中断线，30 秒内重连能回到同一 room/instance。
- resume snapshot 包含坦克位置、血量、比分、剩余时间。
- 超过窗口后返回明确错误。
- reconnect 不导致重复玩家或重复 settlement。

测试：

- unit：resume window。
- integration：disconnect -> reconnect -> resume -> input。
- regression：duplicate login kick/resume 原有逻辑不回归。

### P7：性能、回归与发布门禁

目标：证明新增服务端能力没有破坏主框架，并为 tank demo 建立独立验证入口。

任务：

- 新增 `verify_tank_battle_demo.py`。
- 新增 demo summary：`runtime/validation/tank-battle-demo-summary.json`。
- 增加 tank demo perf smoke：2/20/100 rooms，按固定 tick 和 input rate 运行。
- 增加 framework regression suite 聚合脚本或文档化命令。
- 更新 production docs，明确 demo gate 不进入默认 production evidence。

交付物：

- demo verification script。
- demo performance report。
- 回归测试清单。
- docs/current-state 更新。

验收标准：

- 默认 RC gate 通过。
- SDK full-flow 通过。
- tank demo full-flow 通过。
- tank demo 100 room smoke 不出现崩溃、死锁、明显内存增长。
- 新增代码覆盖核心状态机、simulation、settlement、resume。

测试：

- `ctest --preset default`。
- `python3 scripts/verify_release_candidate.py --skip-release-baseline --soak-profile smoke`。
- `python3 scripts/verify_sdk_full_flow_client.py --build-dir build/default --skip-build`。
- `python3 demo/games/tank_battle/scripts/verify_tank_battle_demo.py --build-dir build/default`。
- 可选固定 runner：business-capacity + tank demo perf smoke。

## 5. 回归约束

任何阶段合入前必须满足：

- 不降低现有 login/room/battle/matchmaking/leaderboard full-flow。
- 不破坏 SDK C++ / C ABI / Python / C# 版本兼容矩阵。
- 不改变默认 plain TCP 生产链路结论。
- 不让 demo 依赖 Redis/TLS/K8s，除非显式 profile 启用。
- 不把 demo gate 接入默认 production evidence。
- 不把业务字段写入框架公共 schema。

必须重点防守的回归点：

| 风险 | 防守方式 |
|---|---|
| gateway route latency 上升 | route latency P99 gate、business-capacity smoke |
| room list 大量房间退化 | pagination unit/perf tests |
| realtime instance 队头阻塞 | per-instance input queue、跨 instance 并发测试 |
| snapshot push 压垮 response | 保持 response high-priority queue，snapshot 降频 |
| settlement 重复提交 | idempotency key tests |
| reconnect 重复绑定 | duplicate login / resume integration tests |
| demo 污染框架 | `rg` 检查业务类型不进入 `include/ src/ sdk/` |

## 6. 建议里程碑

### 第 1 周

- 完成 P0。
- 启动 P1 注册协议和 login backend 单测。
- 完成 demo 服务端目录和 CMake option。

退出标准：

- 文档和目录边界落地。
- register -> login 最小链路在后端测试中通过。

### 第 2 周

- 完成 P1。
- 完成 P2 room list/detail/metadata。
- 开始 owner transfer/kick。

退出标准：

- identity 和 lobby 基础 API 可被通用测试使用。
- 现有 room full-flow 不回归。

### 第 3-4 周

- 完成 P2。
- 完成 P3 realtime instance runtime 最小版本。
- fake plugin 跑通 input -> snapshot。

退出标准：

- 通用 instance lifecycle 可测。
- 现有 battle 兼容路径不回归。

### 第 5-6 周

- 完成 P4 tank simulation。
- 完成 P5 settlement -> leaderboard。
- demo 服务端最小闭环跑通。

退出标准：

- 两玩家 tank demo 从建房到结算全链路通过。
- simulation 单测覆盖移动、碰撞、子弹、得分。

### 第 7 周

- 完成 P6 断线重连。
- 补 resume snapshot 和 reconnect integration。

退出标准：

- 战斗中断线重连可恢复。
- 超时恢复失败有明确错误和 metrics。

### 第 8 周

- 完成 P7 回归、压测和文档收束。
- 固化 demo verification summary。

退出标准：

- RC smoke 通过。
- SDK full-flow 通过。
- tank demo full-flow 通过。
- tank demo perf smoke 通过。

## 7. 最小可交付版本

如果需要先交付一个最小服务端版本，范围应控制为：

- P0 完成。
- P1 register -> login。
- P2 create/list/join/ready。
- P3 instance start/input/snapshot。
- P4 tank movement/fire/collision 最小规则。
- P5 finish -> leaderboard rank。

最小版本可以暂缓：

- kick/owner transfer。
- 复杂匹配。
- 多地图。
- AI 托管。
- 8h soak。
- K8s tank demo 演练。

但不能暂缓：

- 框架/业务隔离。
- 基础单测。
- demo full-flow。
- 默认框架回归 gate。

