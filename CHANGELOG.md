# 更新日志

## v1.1.1 — 基线校准 (2026-05-06)

> **范围**：纯文档基线校准，**不涉及主链协议、业务、运行时行为变更**。
>
> 对应 `docs/development-optimization.md` §11 任务表中的 T01 / T02 / T10 / T12 / T14。

### 文档新增

- 新增 `docs/v1-maturity-matrix.md` — `v1.x` 维护期能力成熟度**单一事实源**。覆盖：网络/协议、业务、治理、配置、持久化、可观测性、工程能力，每项均标注 `stable` / `experimental` / `reserved` / `demo-only`。

### 文档修正（消除"代码事实 / 文档承诺"不一致）

- `README.md`：
  - 新增"版本基线说明"区分 `v1.0.0` 发布版与 `develop` 维护期
  - 全量补成熟度标记，纠正以下过度承诺：
    - 自动分片传输（实际 reserved，主链未接入）
    - TLS 加密（实际 reserved，`GatewayServer` 主链未启用 SSL stream）
    - Token 生命周期失效（实际 experimental，`SessionManager` 不存储过期时间，运行时不主动失效）
    - 登录防爆破（实际 reserved，`LoginService` 未调用 `RateLimiter`）
    - 游客账号（实际 reserved，`max_guests` 主链未引用）
    - 完整热更新（实际 experimental，仅 `max_connections` / `per_ip_connection_limit` 真正应用）
    - 完整管理面 / 管理命令 5001-5005（实际 demo-only，无权限校验）
    - 多进程拆服架构（实际为按模块拆出的独立 demo 入口）
  - 测试规模与压测场景数量的表述按代码事实重新表达（实际 `PressureScenario` 9 个枚举值；ctest 用例以 `ctest -N` 为准）
- `docs/README.md`：重组文档导航，按"v1.x 维护期 / v2.0.0 路线"分组，明确"v2.0.0 在 v1.2.0 决策点前不进入开发"

### 维护版本节奏（来自 `development-optimization.md` §11）

| 版本 | 主题 |
|---|---|
| `v1.1.1` | 基线校准（**本版**） |
| `v1.1.2` | 会话与协议收口（T03 / T04） |
| `v1.1.3` | 入口收敛（T05） |
| `v1.1.4` | 状态边界收敛（T06） |
| `v1.1.5 - v1.1.8` | 业务线收口 |
| `v1.1.9 - v1.1.11` | 治理线收口 |
| `v1.1.12 - v1.1.14` | 运行时装配线收口 |
| `v1.1.15 - v1.1.17` | 持久化/审计/回放横切线收口 |
| `v1.2.0` | 协议与内部结构升级决策点 |
| `v1.2.1 - v1.2.4` | 各主线回归面加固 |

### 兼容性

- 协议、API、配置、运行时行为**完全不变**
- 所有现有测试用例保持通过

---

## v1.0.0 (2026-05-05)

### 核心架构
- 二进制协议：长度前缀 + 消息号 + 请求序号 + 错误码 + 标记位
- Session：异步 TCP + 心跳 + 限频 + 最大包长校验 + 反压保护
- MessageDispatcher：消息注册 + 中间件链 + 按消息范围线程池路由
- SessionManager：认证状态 + 重复登录处理 + 会话迁移
- RoomManager：创建/加入/离开/准备 + 房主机制 + COW 广播快照
- BattleManager：起战斗/结束 + 帧同步（advance_frame）+ 输入历史 + 观战

### 业务服务
- LoginService：三种鉴权模式（dev/json_file/http），Token TTL 24h，顶号踢线
- RoomService：房间生命周期 + 状态广播 + 准备追踪
- BattleService：战斗启动 + 输入路由 + 帧同步 + 结算
- PushService：统一成功/错误/推送响应
- GatewayService：鉴权白名单 + 限频中间件
- AdminService：踢人/封禁/状态/重载管理指令
- MatchmakingService：队列匹配 + ELO 分差控制

### 可观测性
- 10 种累计计数器 + 6 种每秒速率仪表盘
- Prometheus 文本 + JSON 双格式导出
- HTTP 管理端点：/health /metrics /metrics/json
- 请求链路追踪 ID（Session → Dispatcher → Handler）
- 审计日志：登录成功/失败、限频触发、连接拒绝、配置重载
- 崩溃转储：Windows SEH + POSIX 信号
- 日志采样宏：LOG_INFO_SAMPLED / LOG_DEBUG_SAMPLED

### 性能优化
- BufferPool / ObjectPool 复用分配
- 大包自动压缩（>512B）+ 分片传输（>8KB）
- 批量发包（send_batch）+ COW 广播快照
- 零拷贝读包路径 + 写队列反压
- 慢连接检测（积压 > 50% 告警）
- 连接预热（线性提升至全速）

### 安全能力
- Token 生命周期管理（expires_at + TTL）
- 连接限制（总量 + 单 IP）
- 多维限频（连接/用户/消息类型）
- 登录防暴力破解（IP + 用户维度）
- 游客账号（受限权限 + 降速限制）
- TLS 配置（证书 + 私钥 + SSL 上下文）

### 工程能力
- CMake Presets + FetchContent + 本地 third_party 内网构建
- Docker 多阶段构建 + docker-compose + GitHub Actions CI
- 54 个测试（34 单元 + 8 集成 + 7 模糊 + 5 其他）
- 8 种压测场景（echo/invalid_token/slow_echo/broadcast_storm/malicious/battle/chaos/stability）
- 6 个可执行文件

> **维护期补注（自 `v1.1.1` 起，详见 `docs/v1-maturity-matrix.md`）**：
> 以上 v1.0.0 描述中关于"自动分片"、"TLS 上下文已接入主链"、"登录防爆破"、"游客账号"、"完整管理指令"、"完整 Token 生命周期失效"等条目，主链实际为预留或半接入状态，请以 v1.1.1 起的 `docs/v1-maturity-matrix.md` 为准。
