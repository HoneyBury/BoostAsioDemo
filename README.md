# Boost 游戏服务器框架 v1.0.0

基于 Boost.Asio 构建的高性能 C++20 游戏服务器框架。

## 功能概览

### 网络传输层
- 二进制协议：`[4字节长度][2字节消息号][4字节请求序号][4字节错误码][1字节标记位][消息体]`
- 大包自动压缩（>512 字节，`flags::kCompressed` 标记位）
- 超大包分片传输（>8KB 自动拆分为 4KB 分片）
- 批量发包（`Session::send_batch`）用于广播优化
- 零拷贝读包路径（BufferPool 集成）
- 写队列反压保护（防止 OOM）
- TLS 加密支持（`TlsConfig` + `asio::ssl::stream`）

### 业务服务
- **登录**：dev / json_file / http 三种鉴权模式，Token 生命周期管理，重复登录顶号
- **房间**：创建/加入/离开/准备，房主机制，房间状态广播
- **战斗**：起战斗/输入/帧同步/结束/结算，观战模式
- **匹配**：队列匹配，ELO 分数差距控制，可配置匹配人数
- **管理**：踢人/封禁/状态查询/配置重载指令（消息号 5001-5005）

### 可观测性
- Prometheus 指标导出（累计计数器 + 每秒速率仪表盘）
- JSON 指标快照
- HTTP 管理端点：`/health`、`/metrics`、`/metrics/json`
- Grafana 仪表板模板（`grafana/dashboard.json`）
- Prometheus 告警规则（`prometheus/alerts.yml`）
- 请求链路追踪 ID（Session → Dispatcher → Handler 贯穿）
- 安全审计日志（`logs/audit.log`，JSON 行格式）
- 崩溃转储处理（Windows SEH + POSIX 信号）

### 运维能力
- 优雅关闭（SIGINT/SIGTERM，保存状态，排干连接）
- 配置热加载（文件监控自动生效）
- 连接数限制（总量 + 单 IP）
- 速率限制（连接预热 + 用户维度 + 消息类型维度）
- 游客账号支持
- 登录防暴力破解

### 工程化
- CMake + FetchContent + 本地 third_party（支持内网离线构建）
- Docker + docker-compose + 持续集成（GitHub Actions）
- 54 个测试（单元 + 集成 + 模糊测试）
- 8 种压测场景
- 多进程架构（独立的 login / room / battle 服务器）

## 快速开始

```powershell
# 构建
cmake --preset windows-msvc-debug
cmake --build --preset windows-msvc-debug
ctest --preset windows-msvc-debug

# 启动网关
./build/windows-msvc-debug/examples/echo/Debug/echo_server.exe config/gateway.json

# 健康检查
curl http://localhost:9080/health

# 压力测试
./build/windows-msvc-debug/examples/pressure/Debug/gateway_pressure.exe 127.0.0.1 9000 100 10 echo
```

## 示例程序

| 示例 | 路径 | 说明 |
|---|---|---|
| echo_server | `examples/echo/` | 完整网关服务器，展示所有功能模块的组装使用 |
| echo_client | `examples/echo/` | 基础 Echo 客户端，演示协议收发 |
| login_demo | `examples/login_demo/` | 登录流程演示：三种鉴权模式切换、Token 生命周期、重复登录处理 |
| room_demo | `examples/room_demo/` | 房间系统演示：创建/加入/准备/广播，COW 快照广播 |
| battle_demo | `examples/battle_demo/` | 战斗系统演示：帧同步、输入路由、结算、回放录制 |
| admin_demo | `examples/admin_demo/` | 管理工具演示：踢人/封禁/状态查询、健康检查轮询、指标采集 |
| gateway_pressure | `examples/pressure/` | 压力测试工具：8 种场景，支持 JSON 配置 |
| login_server | `examples/login/` | 独立登录服务器（多进程架构） |
| room_server | `examples/room/` | 独立房间服务器（多进程架构） |
| battle_server | `examples/battle/` | 独立战斗服务器（多进程架构） |

## 模块架构

```
include/
├── app/          配置、日志、崩溃处理、审计日志、优雅关闭、热加载
├── net/          会话、协议编解码、消息分发、缓冲池、HTTP管理、
│                 速率限制、服务路由、内部总线、TLS、WebSocket
├── game/
│   ├── gateway/  网关服务器、会话管理、推送服务、管理指令
│   ├── login/    登录服务、Token校验、HTTP远程鉴权
│   ├── room/     房间管理、房间服务
│   ├── battle/   战斗管理、战斗服务、回放播放器
│   ├── match/    匹配服务
│   └── persistence/ 玩家数据存储、SQLite后端
src/              各模块实现文件
examples/         示例程序（echo、login_demo、room_demo 等）
tests/            54 个测试（单元 + 集成 + 模糊）
config/           配置文件（gateway.json、pressure.json 等）
docs/             项目文档（架构规划、开发规范、开发日志等）
```

## 配置文件

完整配置项参见 `config/gateway.json`，包含 TLS、鉴权模式、连接限制、会话参数等。

## 第三方依赖

通过 CMake `FetchContent` 或本地 `third_party/` 目录管理：

- Boost 1.90.0（Asio、Beast）
- fmt 11.2.0
- spdlog 1.15.3
- nlohmann/json 3.12.0
- GoogleTest 1.17.0

内网构建说明参见 `third_party/README.md`。

## 许可证

MIT
