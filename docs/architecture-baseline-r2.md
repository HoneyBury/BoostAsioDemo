# R2 架构微基准闭环

> 日期：`2026-05-16`  
> 范围：Actor runtime 本地投递、跨 core mailbox drain、actor 创建、shutdown per actor 成本、内存分配、SPSC mailbox、Battle world tick。  
> 工具：`v2_arch_benchmark` + `scripts/collect_v2_arch_baseline.py`

## 1. 目标

R2 的重点不是扩大业务功能，而是把架构验收标准中的“未测定”项变成可重复运行的数据入口。当前首批闭环覆盖 Actor runtime：

| 场景 | 指标 | Gate |
|---|---:|---:|
| `actor_local_tell_dispatch` | P99 us | `<= 1000` |
| `actor_cross_core_tell_drain_dispatch` | P99 us | `<= 10000` |
| `actor_create` | P99 us | `<= 10000` |
| `actor_shutdown_per_actor` | P99 us | `<= 5000` |
| `bump_arena_alloc` | P99 us | `<= 10` |
| `object_pool_acquire_release` | P99 us | `<= 50` |
| `spsc_queue_enqueue_dequeue` | P99 us | `<= 10` |
| `battle_world_tick_100_entities` | P99 us | `<= 5000` |
| `actor_fan_in_throughput` | throughput ops/s | `>= 300000`（Debug 防退化 gate） |
| `actor_100k_create_smoke` | samples | `>= 100000` |
| `multi_battle_tick_100_entities` | P99 us | `<= 5000` |

说明：当前 gate 使用文档目标的微秒量级上限，先用于防止明显退化。正式 Release 构建采集稳定后，再按 Release 数据收紧。

## 2. 运行方式

构建：

```powershell
cmake -S . -B build/windows-msvc-debug
cmake --build build/windows-msvc-debug --target v2_arch_benchmark --config Debug
```

直接运行：

```powershell
build\windows-msvc-debug\examples\v2_arch_benchmark\Debug\v2_arch_benchmark.exe --iterations 10000 --actors 10000
```

统一采集：

```powershell
python scripts\collect_v2_arch_baseline.py --build-dir build\windows-msvc-debug --output-root runtime\perf\v2-arch-baseline
```

采集脚本有 `--timeout-seconds`，默认 30 秒；不会启动 gateway/backend 长时间服务。

## 3. 当前 Debug 基线结果

命令：

```powershell
python scripts\collect_v2_arch_baseline.py --build-dir build\windows-msvc-debug --output-root runtime\perf\v2-arch-baseline --timeout-seconds 30
```

结果：

| 场景 | P50 us | P90 us | P99 us | Throughput ops/s |
|---|---:|---:|---:|---:|
| `actor_local_tell_dispatch` | 2.1 | 2.2 | 2.2 | 418309 |
| `actor_cross_core_tell_drain_dispatch` | 3.2 | 3.2 | 3.3 | 268360 |
| `actor_create` | 0.7 | 0.8 | 1.1 | 1086670 |
| `actor_shutdown_per_actor` | 0.36633 | 0.36633 | 0.36633 | 2729780 |
| `bump_arena_alloc` | 0 | 0.1 | 0.1 | 16231100 |
| `object_pool_acquire_release` | 0.1 | 0.1 | 0.1 | 10806100 |
| `spsc_queue_enqueue_dequeue` | 0 | 0.1 | 0.1 | 12014900 |
| `battle_world_tick_100_entities` | 261.8 | 263.9 | 269.7 | 3784.42 |
| `actor_fan_in_throughput` | 1.3 | 1.4 | 1.8 | 511962 |
| `actor_100k_create_smoke` | 0.7 | 0.8 | 1.1 | 1123410 |
| `multi_battle_tick_100_entities` | 373.3 | 432.1 | 484.5 | 2558.11 |

本次 `release_gates.passed=true`。这是 Debug 数据，只证明工具链、采集路径和防退化 gate 可用；正式基线应使用 Release 构建和固定机器重复采集。

## 4. 已扩展覆盖项

`v2_arch_benchmark` 已继续补齐三类非网络微基准：

| 场景 | 覆盖点 |
|---|---|
| `bump_arena_alloc` | `BumpArena::alloc(32)` 指针 bump 分配成本 |
| `object_pool_acquire_release` | `ObjectPool<T>::acquire()` + `release()` 单次往返成本 |
| `spsc_queue_enqueue_dequeue` | `SpscQueue<T>::try_enqueue()` + `try_dequeue()` 单次往返成本 |
| `battle_world_tick_100_entities` | 100 个 participant 下 `battle_world_advance_frame()` 单帧 tick 成本 |
| `actor_fan_in_throughput` | 单 actor 批量入队后一次 dispatch 的 fan-in 吞吐 |
| `actor_100k_create_smoke` | 单进程创建 100K actor 的容量 smoke |
| `multi_battle_tick_100_entities` | 500 场 battle、每场 100 participant 的单帧 tick 成本 |

## 5. Actor 调度公平性与 shutdown 验证

`ActorSystem::dispatch_ready()` 当前采用 ready actor 轮转策略：每次从一个 ready actor 取一条消息处理，若该 actor mailbox 仍有消息则重新入队。这避免单个热 actor 在一次 dispatch 中持续 drain mailbox，从而饿住其他 ready actor。

新增验证：

| 测试 | 覆盖 |
|---|---|
| `V2ActorRuntimeTest.DispatchAllInterleavesReadyActorsFairly` | 多 actor 同时 ready 时按 actor 粒度交错处理 |
| `V2ActorRuntimeTest.ShutdownDuringFairDispatchStopsOtherReadyActors` | dispatch 中触发 shutdown 后不继续投递其他 ready actor |

## 6. 后续扩展

下一批 R2 建议继续补齐：

- Battle runtime 并发多场战斗成本。
- 将 `architecture-acceptance-criteria.md` 中 Actor “未测定”项回填为 R2 数据链接。
