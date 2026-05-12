# v3.x 环境依赖与生产就绪规划

> 状态: 规划阶段 | 版本: v3.1.0 目标

## 1. 当前状态

| 组件 | 配置 | 代码集成 | 生产就绪 |
|------|------|---------|---------|
| Redis | docker-compose + K8s + redis.conf | ❌ 内存 SortedSet | 否 |
| K8s CRD | gameserver-crd.yaml | ❌ 未部署验证 | 否 |
| K8s Deploy | gateway/backend/redis Deployment | ❌ 未部署验证 | 否 |
| Helm | Chart.yaml + values.yaml | ❌ 未部署验证 | 否 |
| Prometheus | prometheus.yml | ✅ /metrics | 是 |
| Grafana | dashboard.json | ✅ 端点可用 | 是 |
| Docker | Dockerfile × 2 + compose | ⚠️ 缺构建步骤 | 否 |
| TLS | tls_config.h | ❌ 未接入 asio::ssl | 否 |
| CI/CD | github-actions.yml | ✅ 基础流水线 | 部分 |

## 2. Phase E1: Redis 集成

### 目标
Leaderboard 和 EventStore 从内存切换到 Redis 持久化存储。

### 依赖
- `redis-plus-plus` (C++ client, header-only + hiredis)
- `hiredis` (C Redis client)
- CMake FetchContent 拉取

### 改动
- 添加 `RedisLeaderboard` 类实现 `IEventStore` 接口
- `LeaderboardService` 支持 Redis 后端（FeatureFlag 切换）
- `FileEventStore` 增加 Redis Streams 可选后端
- 连接池: `RedisConnectionPool`

### 验收
- Leaderboard 数据在服务重启后保持
- Redis 不可用时回退到内存模式

## 3. Phase E2: Docker 生产构建

### 目标
Docker 镜像可构建、可运行、可通过 compose 编排。

### 改动
- Dockerfile: 添加 CMake 构建步骤（multi-stage）
- docker-compose: 完善健康检查、卷挂载
- 构建脚本: `scripts/build_docker.sh`

### 验收
- `docker-compose up` 启动 6 服务 + Redis + Prometheus + Grafana
- `/health` 端点返回 healthy

## 4. Phase E3: TLS/mTLS 安全传输

### 目标
服务间通信加密，支持 mTLS。

### 依赖
- OpenSSL (Boost.Asio SSL 需要)
- 自签证书生成脚本

### 改动
- `BackendConnection` 增加 SSL 模式
- `SecurityPolicy` 接入 `GatewayServiceBridge`
- TLS 证书 ConfigWatcher 热更新
- FeatureFlag `v3_tls_enabled` 控制灰度

### 验收
- 服务间 TLS 加密通信
- 证书过期自动告警

## 5. Phase E4: K8s 部署验证

### 目标
在 minikube/kind 上验证全部 K8s 配置可用。

### 改动
- 完善 Deployment: 资源限制、探针、反亲和
- ConfigMap 生成脚本
- `kubectl apply -f env/k8s/` 一键部署
- 水平自动扩缩(HPA)配置

### 验收
- `kubectl get pods` 显示全部服务 Running
- 端口转发后 `/health` 可达

## 6. Phase E5: K8s Operator 实现

### 目标
GameServer CRD 的 Controller 实现，自动化运维。

### 技术选型
- Go + controller-runtime 或 Python + kopf
- 打包为 Docker 镜像，部署在集群内

### 功能
- 监听 GameServer CR 变更
- 自动创建/更新 Deployment + Service
- 滚动更新（先排干再更新）
- 水平扩缩容（基于 metrics）

## 7. 版本规划

```
v3.0.0: 分布式运行时核心 (已完成, 655 tests)
v3.1.0: E1 Redis + E3 TLS
v3.2.0: E2 Docker + E4 K8s 验证
v3.3.0: E5 K8s Operator
```
