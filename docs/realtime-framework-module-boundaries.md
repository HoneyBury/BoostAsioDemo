# 实时系统框架模块边界

更新时间：2026-05-22

本文档用于代码 review、架构评审和 demo 接入评审。任何新功能都必须先判断属于框架通用能力还是业务样例能力。

## 1. 边界原则

- 框架层提供通用实时网络能力。
- 业务层提供具体领域规则。
- 框架层可以保存业务 opaque payload，但不能解释具体游戏实体。
- 业务层可以依赖 SDK、gateway、room、matchmaking、leaderboard 和 realtime instance，但不能反向要求框架增加业务专属类型。
- 默认生产 gate 只证明框架能力；demo gate 证明某个样例能运行。

## 2. Gateway

允许：

- TCP/TLS 接入、连接生命周期、session 管理。
- 协议编解码、typed envelope、schema validation。
- auth whitelist、rate limit、backpressure、连接保护。
- backend route、timeout、retry policy、RED metrics。
- push/response 出站优先级和诊断指标。

禁止：

- `Tank`、`Bullet`、`Map` 等业务类型。
- 根据业务规则决定胜负或得分。
- 为单一 demo hardcode message id 路由以外的业务解释。

验收标准：

- gateway 新增逻辑可被至少两个不同实时系统复用。
- 新增指标以服务、路由、延迟、错误、连接为主，业务标签保持可选。

## 3. Identity / Login Backend

允许：

- 注册、登录、token refresh、guest login。
- token/JWT 校验、生产模式禁用 dev token fallback。
- duplicate login、session kick、session resume metadata。
- 账号状态、封禁状态、基础 profile。

禁止：

- 游戏新手奖励。
- 坦克初始属性、皮肤、道具。
- 具体 demo 的段位、阵营、角色初始化。

验收标准：

- identity API 可用于游戏、实时协作、IoT 控制、推送网关等场景。
- 登录响应只包含通用身份和 session 信息。

## 4. Lobby / Room Backend

允许：

- create/list/detail/join/leave room。
- ready/unready、kick、owner transfer。
- capacity、visibility、metadata、room state push。
- room 生命周期、空房清理、in-instance 标记。

禁止：

- 解释具体地图、出生点、队伍胜负。
- 在 room 层执行业务 simulation。
- 把 `room metadata` 固化成某个 demo 的结构体。

验收标准：

- room metadata 作为 opaque payload 或通用 key/value。
- room API 可服务坦克大战、实时协作房间、压测会话等不同 demo。

## 5. Matchmaking Backend

允许：

- join/leave/status。
- mode、rating、region、latency bucket、party size 等通用匹配条件。
- match result 输出 room/instance 创建建议。
- Redis/Raft profile 下的匹配状态恢复或复制。

禁止：

- 解释坦克职业、地图权重、道具规则。
- 将某个 demo 的分队算法固化为唯一逻辑。

验收标准：

- 匹配请求支持 opaque business criteria。
- 框架默认匹配算法保持可替换。

## 6. Realtime Instance / Battle Runtime

允许：

- instance lifecycle。
- player attach/detach。
- tick scheduling。
- input queue and ordering。
- snapshot push。
- resume payload hook。
- settlement event hook。
- replay/snapshot/recovery hook。
- per-instance metrics。

禁止：

- 碰撞检测。
- 子弹移动。
- 坦克 HP。
- 具体游戏胜负判定。
- 具体游戏得分公式。

验收标准：

- runtime 不依赖业务实体类型。
- simulation 可作为 plugin 或 demo backend 独立替换。
- settlement 输出转为通用 `user_id/display_name/score/idempotency_key/source/context` 后提交 leaderboard。

## 7. Leaderboard Backend

允许：

- submit/top/rank。
- idempotency key。
- season、board key、mode、region 等通用维度。
- Redis 持久化、降级内存、恢复验证。

禁止：

- 写死“击杀分”“坦克胜利分”等规则。
- 在 leaderboard 内重新计算业务胜负。

验收标准：

- 业务只提交已经计算好的 score/result。
- leaderboard 不知道 score 是来自坦克大战、匹配系统还是其他实时系统。

## 8. Persistence / Recovery

允许：

- snapshot store。
- event log。
- write-behind。
- result store。
- Redis/Raft profile。
- replay/recovery gate。

禁止：

- 将业务实体作为框架固定 schema。
- 让框架数据层承担业务规则迁移。

验收标准：

- 框架保存业务 payload 时保留 version、schema id、source。
- 业务 schema 由 demo 自己维护。

## 9. Observability / Operations

允许：

- gateway/backend RED metrics。
- latency histogram。
- health/ready/diagnostics。
- trace/audit。
- deployment, rollback, recovery runbook。

禁止：

- demo 指标替代框架 SLO。
- demo 成功率冒充生产 readiness。

验收标准：

- 框架 SLO 和 demo SLO 分开命名。
- demo metrics 可以挂 business label，但默认 dashboard 仍以框架健康为主。

## 10. Demo Business

允许：

- 独立业务协议。
- 独立 simulation。
- 独立资源、地图、客户端。
- 独立 demo verification。
- 通过框架通用 API 接入。

禁止：

- 修改框架默认 API 来适配单一 demo。
- 将 demo failure 纳入默认 release 阻断。
- 在 SDK 公共层暴露业务专属方法。

验收标准：

- demo 可以删除而不影响框架构建、SDK 分发和生产 gate。
- demo 只能通过文档声明验证了哪些框架能力。

