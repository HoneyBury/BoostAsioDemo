# 服务发现与集群路由指南

> 适用版本：v3.0.0+
> 事实源：`v3::cluster::ClusterRouter`、`v2::service::ServiceRegistrar`、`v2::gateway::GatewayServiceBridge`

## 概述

本项目的服务发现基于 ClusterRouter 实现，支持以下核心能力：

- **动态服务注册/注销**：后端服务实例可动态加入或离开集群
- **健康检查**：自动检测后端实例健康状态，故障时自动摘除
- **负载均衡**：Round-robin 策略在健康实例之间分配请求
- **优雅排空**：服务关停前进入 Draining 状态，等待已有请求完成
- **静态回退**：ClusterRouter 不可用时自动回退到静态 BackendConfig
- **线程安全**：所有 ClusterRouter 操作均通过 `std::mutex` 保护

## 架构说明

```
┌─────────────────────────────────────────────────────────┐
│                      Gateway                             │
│  ┌──────────────────────────────────────────────┐       │
│  │         GatewayServiceBridge                  │       │
│  │  route() → resolve_backend()                  │       │
│  │     ├── ClusterRouter (优先级高)              │       │
│  │     └── Static BackendConfig (fallback)       │       │
│  └──────────────────────┬───────────────────────┘       │
│                         │                                │
│  ┌──────────────────────▼───────────────────────┐       │
│  │            ClusterRouter                      │       │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐   │       │
│  │  │  login   │  │   room   │  │  battle  │.. │       │
│  │  │ RR idx:2 │  │ RR idx:0 │  │ RR idx:1 │   │       │
│  │  │ inst[0]  │  │ inst[0]  │  │ inst[0]  │   │       │
│  │  │ inst[1]  │  └──────────┘  └──────────┘   │       │
│  │  └──────────┘                               │       │
│  │                                              │       │
│  │  health_check_fn_ → TCP connect per node     │       │
│  │  run_health_checks() → 每 5s 后台线程调用     │       │
│  └──────────────────────────────────────────────┘       │
│                         ▲                                │
│  ┌──────────────────────┴───────────────────────┐       │
│  │         ServiceRegistrar (per backend)        │       │
│  │  start() → 注册 + 心跳线程                   │       │
│  │  stop()  → drain + 注销                      │       │
│  │  heartbeat_loop() → 每 5s 更新 last_heartbeat│       │
│  └──────────────────────────────────────────────┘       │
└─────────────────────────────────────────────────────────┘
```

### 组件职责

| 组件 | 命名空间 | 职责 |
|------|----------|------|
| `ClusterRouter` | `v3::cluster` | 核心路由表，管理实例注册/发现/健康检查 |
| `ServiceRegistrar` | `v2::service` | 自动注册 + 心跳管理，简化后端接入 |
| `GatewayServiceBridge` | `v2::gateway` | 网关路由，优先使用 Router，失败回退静态配置 |

## 如何在新的后端服务中使用

### 方式一：通过 ServiceRegistrar 自动管理（推荐）

```cpp
#include "v2/service/service_registrar.h"
#include "v3/cluster/cluster_router.h"

// 创建 ClusterRouter（通常由网关创建）
auto router = std::make_shared<v3::cluster::ClusterRouter>();

// 设置健康检查函数（网关到后端的 TCP 连接检查）
router->set_health_check([](const v3::cluster::NodeId& node) -> bool {
    // TCP connect 到 node.host:node.port
    // 返回 true 表示健康
});

// 创建 ServiceRegistrar 并注册
auto registrar = std::make_shared<v2::service::ServiceRegistrar>(
    *router,
    "login",         // 服务名
    "127.0.0.1",     // 主机
    9302,            // 端口
    "login-node-1",  // 节点唯一名（可选，默认 "host:port"）
    std::chrono::seconds(5)  // 心跳间隔
);

// 设置自定义健康检查回调（可选）
registrar->set_health_check_fn([]() -> bool {
    // 检查本地服务是否健康
    return true;
});

// 启动注册 + 心跳
registrar->start();

// 停止时注销（析构函数自动调用 stop()）
registrar->stop();
```

### 方式二：直接使用 ClusterRouter API

```cpp
auto router = std::make_shared<v3::cluster::ClusterRouter>();

// 注册实例
v3::cluster::ServiceInstance instance;
instance.node.host = "127.0.0.1";
instance.node.port = 9302;
instance.node.node_name = "login-1";
instance.service_name = "login";
instance.state = v3::cluster::ServiceState::kHealthy;
router->register_service(instance);

// 发现实例（round-robin）
auto discovered = router->discover("login");
if (discovered) {
    // 使用 discovered->node.host 和 discovered->node.port
}

// 标记健康状态
router->mark_unhealthy("login", node);
router->mark_healthy("login", node);

// 启动排空
router->start_drain("login", node);

// 注销
router->deregister_service("login", node);
```

### 在 GatewayServiceBridge 中使用

```cpp
// 创建 bridge 并设置 cluster router
auto bridge = std::make_unique<GatewayServiceBridge>(...);
auto router = std::make_shared<v3::cluster::ClusterRouter>();
bridge->set_cluster_router(router);

// 路由请求时，resolve_backend() 自动优先查询 ClusterRouter
auto resolved = bridge->resolve_backend(v2::service::ServiceId::kLogin);
// resolved->from_cluster == true 表示来自动态发现
// resolved->from_cluster == false 表示来自静态回退

// 如果 ClusterRouter 返回 nullopt，自动回退到静态 BackendConfig
```

## 健康检查配置参数

`v3::cluster::HealthCheckConfig` 结构体控制健康检查行为：

| 参数 | 类型 | 默认值 | 说明 |
|------|------|--------|------|
| `interval` | `std::chrono::milliseconds` | 5000ms | 健康检查周期 |
| `timeout` | `std::chrono::milliseconds` | 2000ms | 单次健康检查超时 |
| `failure_threshold` | `uint32_t` | 3 | 连续失败次数超过此值标记为 Unhealthy |
| `recovery_threshold` | `uint32_t` | 2 | 连续成功次数超过此值恢复为 Healthy |
| `drain_timeout` | `std::chrono::milliseconds` | 30000ms | Draining 状态下最大等待时间，超时后标记 Unhealthy |

### 配置示例

```cpp
v3::cluster::HealthCheckConfig config;
config.interval = std::chrono::seconds(5);
config.timeout = std::chrono::seconds(2);
config.failure_threshold = 3;   // 3 次失败 → unhealthy
config.recovery_threshold = 2;  // 2 次成功 → healthy
config.drain_timeout = std::chrono::seconds(30);

auto router = std::make_shared<v3::cluster::ClusterRouter>(config);
```

### 健康检查函数

健康检查函数接受 `const NodeId&` 返回 `bool`：

```cpp
router->set_health_check([](const v3::cluster::NodeId& node) -> bool {
    try {
        boost::asio::io_context io;
        boost::asio::ip::tcp::socket socket(io);
        boost::asio::ip::tcp::endpoint ep(
            boost::asio::ip::make_address(node.host), node.port);
        boost::system::error_code ec;
        socket.open(ep.protocol(), ec);
        if (ec) return false;
        socket.connect(ep, ec);
        if (ec) return false;
        socket.close();
        return true;
    } catch (...) {
        return false;
    }
});
```

## 故障排除指南

### 症状：服务始终路由到静态配置

1. **检查 ClusterRouter 是否已设置**
   ```cpp
   if (bridge->get_cluster_router() == nullptr) {
       // 未设置 cluster router，需要调用 set_cluster_router()
   }
   ```

2. **检查实例是否已注册**
   ```cpp
   auto table = router->route_table();
   // 检查 table 中是否有对应服务名和实例
   ```

3. **检查实例健康状态**
   ```cpp
   auto healthy = router->healthy_count("login");
   auto unhealthy = router->unhealthy_count("login");
   // 如果 healthy == 0，discover() 返回 nullopt，触发回退
   ```

4. **检查健康检查函数的返回值**
   - 健康检查函数返回 `false` 会导致实例被标记为 Unhealthy
   - 确认 TCP connect 到后端服务是可达的

### 症状：健康检查频繁误报

1. 增大 `failure_threshold`（如从 3 改为 5）减少误报
2. 减小 `interval` 加快检测响应速度
3. 确保健康检查函数内部有超时机制，避免阻塞

### 症状：服务停止后仍有流量

1. 确认调用了 `start_drain()` 先排空，再调用 `deregister_service()`
2. 检查 drain_timeout 是否足够长
3. 排空期间 `discover()` 不会返回该实例

### 日志关键字

| 日志级别 | 关键字 | 说明 |
|----------|--------|------|
| INFO | `GatewayServiceBridge: resolved ... via cluster router` | 成功通过 ClusterRouter 发现后端 |
| INFO | `GatewayServiceBridge: cluster router returned no healthy instance` | ClusterRouter 无健康实例 |
| WARN | `GatewayServiceBridge: falling back to static config` | 回退到静态配置 |
| INFO | `ServiceRegistrar: registered ...` | 服务注册成功 |
| INFO | `ServiceRegistrar: deregistered ...` | 服务注销成功 |
| WARN | `ServiceRegistrar: ... heartbeat FAILED` | 心跳检测失败 |

## 向后兼容性

- 如果 `ClusterRouter` 未设置或未注册任何实例，`GatewayServiceBridge::resolve_backend()` 自动回退到静态 `BackendConfig`
- 所有现有静态配置路径不受影响
- 无需修改现有业务代码即可启用动态服务发现
- 网关的 `DemoServer::load_gateway_config()` 同时支持静态配置更新和 ClusterRouter 注册

## 参考实现

- `include/v3/cluster/cluster_router.h` — ClusterRouter 完整声明
- `src/v3/cluster/cluster_router.cpp` — ClusterRouter 完整实现
- `include/v2/service/service_registrar.h` — ServiceRegistrar 声明
- `src/v2/service/service_registrar.cpp` — ServiceRegistrar 实现
- `src/v2/gateway/gateway_service_bridge.cpp` — GatewayServiceBridge resolve_backend()
- `tests/v2/unit/cluster_router_test.cpp` — 单元测试
- `tests/v2/integration/cluster_router_e2e_test.cpp` — 端到端集成测试
