# BoostAsioDemo v2.0.0 部署手册

## 1. 架构概览

v2.0.0 包含 4 个服务进程：

```
客户端 ──▶ gateway (9000) ──▶ login-backend  (9202)
                 │            ▶ room-backend   (9302)
                 │            ▶ battle-backend (9303)
                 │
            mgmt (9080)  — /health, /metrics
```

| 服务 | 端口 | 二进制文件 | 职责 |
|---|---|---|---|
| gateway | 9000, 9080 | `v2_gateway_demo` | 客户端接入、协议桥接、路由到后端 |
| login-backend | 9202 | `v2_login_backend` | 登录鉴权、token 校验、会话管理 |
| room-backend | 9302 | `v2_room_backend` | 房间创建/加入/准备、成员管理 |
| battle-backend | 9303 | `v2_battle_backend` | 战斗帧同步、ECS world tick、结算/回放 |

gateway 是无状态接入层，可以水平扩展（配合 SO_REUSEPORT）。三个后端各自独立部署。

## 2. 前置条件

- **系统**: Linux (Ubuntu 24.04+)、macOS 14+，或 Docker
- **编译器**: Clang 18+ 或 GCC 13+（需支持 C++20）
- **CMake**: 3.21+
- **Boost**: 1.86+（通过 `third_party/download_deps.sh` 自动获取）
- **端口**: 9000, 9080, 9202, 9302, 9303 未被占用

## 3. 从源码构建

```bash
git clone <repo-url> && cd BoostAsioDemo
git checkout develop

# 下载 Boost 依赖
bash third_party/download_deps.sh

# 配置和构建
cmake --preset default
cmake --build --preset default --parallel

# 运行测试
ctest --test-dir build/default

# 安装到系统路径
cmake --install build/default --prefix /usr/local
```

安装后目录布局：

```
/usr/local/
├── bin/
│   ├── v2_gateway_demo
│   ├── v2_login_backend
│   ├── v2_room_backend
│   └── v2_battle_backend
└── share/boost_gateway/
    ├── config/
    │   ├── gateway.json
    │   ├── login_backend.json
    │   ├── room_backend.json
    │   └── battle_backend.json
    └── docs/
        ├── deploy.md
        └── ...
```

## 4. Docker Compose 部署

### 4.1 快速启动（单节点开发/测试）

```bash
# 构建镜像并启动全部 4 个服务
docker-compose up -d

# 查看状态
docker-compose ps

# 查看日志
docker-compose logs -f gateway
docker-compose logs -f login-backend

# 停止
docker-compose down
```

### 4.2 带 Mock Auth 的完整栈

```bash
docker-compose --profile full up -d
```

### 4.3 健康检查

```bash
# Gateway 管理端点
curl http://localhost:9080/health

# 各后端健康检查（如果后端暴露了 HTTP 健康端点）
curl http://localhost:9202/health
curl http://localhost:9302/health
curl http://localhost:9303/health
```

### 4.4 单独启动某个服务

```bash
# 只启动后端，gateway 单独调试
docker-compose up -d login-backend room-backend battle-backend
./build/default/examples/v2_gateway_demo/v2_gateway_demo \
    --login-host 127.0.0.1 --login-port 9202 \
    --room-host 127.0.0.1 --room-port 9302 \
    --battle-host 127.0.0.1 --battle-port 9303
```

## 5. systemd 部署（Linux 生产环境）

### 5.1 安装

```bash
# 1. 从源码安装二进制文件
cmake --install build/default --prefix /usr/local

# 2. 创建运行用户
useradd -r -s /bin/false -d /var/lib/boost-gateway boost-gateway

# 3. 创建目录
mkdir -p /var/lib/boost-gateway/{login,room,battle}
mkdir -p /var/log/boost-gateway
chown -R boost-gateway:boost-gateway /var/lib/boost-gateway /var/log/boost-gateway

# 4. 安装 systemd 单元文件
cp deploy/systemd/*.service /etc/systemd/system/

# 5. 重载并启用
systemctl daemon-reload
systemctl enable boost-login-backend boost-room-backend boost-battle-backend boost-gateway

# 6. 按依赖顺序启动（后端起→gateway 最后）
systemctl start boost-login-backend boost-room-backend boost-battle-backend
sleep 2  # 等待后端就绪
systemctl start boost-gateway
```

### 5.2 日常运维

```bash
# 查看全部服务状态
systemctl status 'boost-*'

# 查看日志
journalctl -u boost-gateway -f
journalctl -u boost-login-backend -n 100

# 重启单个服务
systemctl restart boost-room-backend

# 全部停止
systemctl stop boost-gateway boost-battle-backend boost-room-backend boost-login-backend
```

### 5.3 定制配置

systemd 单元文件默认从 `/usr/local/share/boost_gateway/config/` 读取配置（只读挂载）。
如需修改端口或其他参数，编辑对应的 unit 文件中的 `ExecStart=` 行：

```bash
systemctl edit boost-login-backend --full
systemctl daemon-reload
systemctl restart boost-login-backend
```

## 6. 配置说明

所有配置文件位于 `config/` 目录：

| 文件 | 用途 | 关键参数 |
|---|---|---|
| `gateway.json` | Gateway 主配置 | `port:9000`, `http_management_port:9080`, `io_threads`, v2_shadow_bridge flags |
| `login_backend.json` | 登录后端 | `port:9202`, `auth.provider`, `session_timeout_ms` |
| `room_backend.json` | 房间后端 | `port:9302`, `max_rooms`, `room_idle_timeout_ms` |
| `battle_backend.json` | 战斗后端 | `port:9303`, `tick_rate_ms`, `archive.replay_path` |

配置文件修改后需重启对应服务生效。

## 7. 监控与可观测性

### 7.1 指标端点

- Gateway Prometheus 指标：通过 `gateway.json` 中 `metrics_prometheus_path` 配置路径落盘
- 各后端指标：通过各自配置文件的 `metrics_*` 字段落盘
- HTTP 管理端点（`:9080`）：`/health`、`/metrics`

### 7.2 Prometheus + Grafana

项目提供预配置的监控栈：

```bash
# Prometheus 告警规则
prometheus/alerts.yml

# Grafana 仪表板
grafana/dashboard.json
```

> **注意**：当前 Prometheus 和 Grafana 配置主要覆盖 v1 指标。v2 后端指标需手动更新 `alerts.yml` 和 `dashboard.json`。

### 7.3 日志

所有服务输出到 stdout/stderr，Docker 和 systemd 均会收集。建议配合 `journald` 或 Docker logging driver 做日志聚合。

## 8. 扩容

### Gateway 水平扩容（SO_REUSEPORT）

Gateway 已支持 `SO_REUSEPORT`，在同一主机上启动多个 gateway 实例即可实现多核 ingress 负载分担：

```bash
# 通过 systemd 模板启动多个 gateway 实例
# (需要将 unit 改为 template: boost-gateway@.service)
systemctl start boost-gateway@0 boost-gateway@1 boost-gateway@2
```

### 后端扩容

- **login/room backend**：通过 `ServiceRegistry` 注册多个实例，gateway 自动负载均衡
- **battle backend**：每场战斗绑定到单个 backend 实例，由 room 在开战时分配

## 9. 故障恢复

| 故障 | 影响 | 恢复 |
|---|---|---|
| login-backend 宕机 | 新登录失败，已登录玩家无影响 | systemd/Docker 自动重启（2s） |
| room-backend 宕机 | 房间操作失败，gateway 自动摘除 | 自动重启 + `ServiceRegistry` TTL 摘除/恢复 |
| battle-backend 宕机 | 进行中战斗中断，结算丢失 | 自动重启，客户端需重新开战 |
| gateway 宕机 | 所有客户端断开 | 自动重启 + 客户端重连 |

所有服务配置 `Restart=on-failure`（systemd）或 `restart: unless-stopped`（Docker），支持自动恢复。

## 10. 安全注意事项

- systemd 单元已启用 `ProtectSystem=strict`、`NoNewPrivileges=yes`
- 生产环境请配置反向代理（nginx/Envoy）做 TLS 终止和 DDoS 防护
- 管理端点（`:9080`）不应暴露到公网
- 生产环境建议使用专用鉴权 provider 替换 `"provider": "dev"`
