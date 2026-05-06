# 更新日志

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
