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
| `actor_local_tell_dispatch` | 2.1 | 2.1 | 3.3 | 419386 |
| `actor_cross_core_tell_drain_dispatch` | 3.1 | 3.2 | 3.2 | 270216 |
| `actor_create` | 0.7 | 0.8 | 1.1 | 1086670 |
| `actor_shutdown_per_actor` | 0.36455 | 0.36455 | 0.36455 | 2743110 |
| `bump_arena_alloc` | 0 | 0.1 | 0.1 | 15865500 |
| `object_pool_acquire_release` | 0.1 | 0.1 | 0.1 | 10704300 |
| `spsc_queue_enqueue_dequeue` | 0 | 0.1 | 0.1 | 11946000 |
| `battle_world_tick_100_entities` | 263.8 | 265.9 | 284.5 | 3738.6 |

本次 `release_gates.passed=true`。这是 Debug 数据，只证明工具链、采集路径和防退化 gate 可用；正式基线应使用 Release 构建和固定机器重复采集。

## 4. 已扩展覆盖项

`v2_arch_benchmark` 已继续补齐三类非网络微基准：

| 场景 | 覆盖点 |
|---|---|
| `bump_arena_alloc` | `BumpArena::alloc(32)` 指针 bump 分配成本 |
| `object_pool_acquire_release` | `ObjectPool<T>::acquire()` + `release()` 单次往返成本 |
| `spsc_queue_enqueue_dequeue` | `SpscQueue<T>::try_enqueue()` + `try_dequeue()` 单次往返成本 |
| `battle_world_tick_100_entities` | 100 个 participant 下 `battle_world_advance_frame()` 单帧 tick 成本 |

## 5. 后续扩展

下一批 R2 建议继续补齐：

- Battle runtime 并发多场战斗成本。
- 将 `architecture-acceptance-criteria.md` 中 Actor “未测定”项回填为 R2 数据链接。
