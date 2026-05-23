# P0 性能优化验收报告 — Windows Release Baseline

> 日期：`2026-05-23`
> 提交：`8387a13dcb38bee9be62e11b055b11225676b292`
> 构建目录：`build/Release` (Release 模式)
> 工具：`scripts/collect_v2_perf_baseline.py --run-preset baseline --repetitions 3 --backend-pool-size 8`

## 优化清单

| # | 优化项 | 文件 | 变更 |
|---|--------|------|------|
| P0.2a | 后端连接池扩容 | `gateway_service_bridge.cpp` | 默认池大小 1→4（压测时通过 env 设置为 8） |
| P0.2b | 战斗路由卸载线程数 | `runtime.cpp` | 默认工作线程 0→4 |
| P0.2c | CircuitBreaker 线程安全 | `circuit_breaker.h/.cpp` | 添加 `std::mutex`，所有访问路径加锁 |
| P0.2d | Windows 高精度定时器 | `highres_timer.h` + 6 个进程 | RAII `timeBeginPeriod(1)` 消除 15.6ms 休眠粒度 |
| P0.2e | 头文件循环依赖修复 | `runtime.h` | 用前向声明替换直接 include |

## 基线结果

### Overall: **PASS** ✅

| Case | 阈值 | 优化前 | 优化后 | 提升 |
|------|------|--------|--------|------|
| `echo-100-30s` | P99 ≤ 50ms | 100ms ❌ | **1ms** ✅ | **100x** |
| `echo-1000-30s` | P99 ≤ 50ms | 150ms ❌ | **5ms** ✅ | **30x** |
| `battle-20-30s` | P99 ≤ 100ms | 750ms ❌ | **10ms** ✅ | **75x** |
| `battle-100-30s` | P99 ≤ 250ms | 5000ms ❌ | **200ms** ✅ | **25x** |

### 详细指标

| Case | Throughput | P50 | P90 | P99 | 失败 | 拒绝 |
|------|-----------|-----|-----|-----|------|------|
| echo-100-30s | 1,945/s | 1ms | 1ms | 1ms | 0 | 0 |
| echo-1000-30s | 17,846/s | 1ms | 2ms | 5ms | 0 | 0 |
| battle-20-30s | 553/s | 5ms | 5ms | 10ms | 0 | 0 |
| battle-100-30s | 1,424/s | 100ms | 100ms | 200ms | 0 | 0 |

### 后端延迟

| Service | Avg | P99 | 请求数 |
|---------|-----|-----|--------|
| login | 2.8ms | 5ms | 3,660 |
| room | 3.7ms | 5ms | 900 |
| battle | 2.3ms | 5ms | 50,251 |

### 内存成本

| Case | RSS | Delta | KB/连接 | 线程 |
|------|-----|-------|---------|------|
| echo-100 | 10.0 MB | 0.8 MB | 8.4 | 20 |
| echo-1000 | 12.6 MB | 3.4 MB | 3.5 | 23 |
| battle-20 | 12.8 MB | 3.6 MB | 185 | 29 |
| battle-100 | 13.4 MB | 4.2 MB | 42.5 | 34 |

## 根因分析

### 1. `sleep_for(1ms)` 在 Windows 上实际休眠 ~15.6ms

`src/v2/service/backend_frame_codec.cpp` 中 `read_exactly_with_timeout()` 使用非阻塞 socket + `std::this_thread::sleep_for(1ms)` 轮询。Windows 默认定时器分辨率 64Hz (15.6ms)，导致每次休眠实际挂起 ~15.6ms。

一次后端 RTT 经过 2 次 `read_exactly_with_timeout`（网关读响应 + 后端读请求），合计 ~31ms 人为延迟。

**修复**: `HighResTimer` RAII 类调用 `timeBeginPeriod(1)`，将定时器分辨率提升至 1ms。后端延迟从 30ms→2.8ms，P99 从 50ms→5ms。

### 2. 后端连接池过小

默认池大小=1 导致所有请求序列化通过单个 TCP 连接。

**修复**: 池大小 1→4（压测时设为 8），允许并发后端请求。

### 3. 战斗路由阻塞 actor 线程

战斗相关的后端 IO 在 actor 线程上同步执行，阻塞消息处理。

**修复**: 战斗路由卸载到独立工作线程池，默认 4 线程。

### 4. CircuitBreaker 数据竞争

`gateway_service_bridge.cpp` 中 `slot.breaker` 在多线程访问时没有锁保护。

**修复**: 添加 `std::mutex`，所有 `on_success/on_failure/allow_request` 加锁。

## 已知问题

1. **battle-100 P99 尾部抖动**: 约 1.5% 消息落在 200-300ms 区间，由 actor 线程广播推送造成。单线程 actor 在 100 人帧广播时产生排队。后续优化可考虑将广播推送卸载到工作线程。
2. **集成测试编译失败**: `cluster_router_e2e_test.cpp` 访问 `GatewayServiceBridge::resolve_backend`（private），`windows_platform_test.cpp` 类型转换错误。需在 header 中添加 `friend` 声明或修复测试代码。
3. **Redis 测试跳过**: Redis 相关测试需要外部 Redis 实例，默认环境已正确跳过。
4. **hiredis.dll 部署**: Release 构建后需手动复制 hiredis.dll 到各后端目录，CMake 默认 install 步骤未覆盖。

## 产物

- 基线报告: `runtime/perf/20260523-165827/report.md`
- 原始数据: `runtime/perf/20260523-165827/summary.json`
- 各轮结果: `runtime/perf/20260523-165827/results/*.result.json`
- 网关诊断: `runtime/perf/20260523-165827/results/*.gateway.diagnostics.json`
- 进程日志: `runtime/perf/20260523-165827/logs/*.log`
