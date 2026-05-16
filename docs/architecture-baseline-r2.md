# R2 架构微基准闭环

> 日期：`2026-05-16`  
> 范围：Actor runtime 本地投递、跨 core mailbox drain、actor 创建、shutdown per actor 成本。  
> 工具：`v2_arch_benchmark` + `scripts/collect_v2_arch_baseline.py`

## 1. 目标

R2 的重点不是扩大业务功能，而是把架构验收标准中的“未测定”项变成可重复运行的数据入口。当前首批闭环覆盖 Actor runtime：

| 场景 | 指标 | Gate |
|---|---:|---:|
| `actor_local_tell_dispatch` | P99 us | `<= 1000` |
| `actor_cross_core_tell_drain_dispatch` | P99 us | `<= 10000` |
| `actor_create` | P99 us | `<= 10000` |
| `actor_shutdown_per_actor` | P99 us | `<= 5000` |

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

## 3. 当前 Smoke 结果

命令：

```powershell
build\windows-msvc-debug\examples\v2_arch_benchmark\Debug\v2_arch_benchmark.exe --iterations 1000 --actors 1000 --output runtime\perf\v2-arch-benchmark-smoke.json
```

结果：

| 场景 | P50 us | P90 us | P99 us | Throughput ops/s |
|---|---:|---:|---:|---:|
| `actor_local_tell_dispatch` | 2.2 | 2.3 | 2.3 | 392542 |
| `actor_cross_core_tell_drain_dispatch` | 3.3 | 3.3 | 3.4 | 260092 |
| `actor_create` | 0.7 | 0.8 | 7.8 | 1023330 |
| `actor_shutdown_per_actor` | 0.3508 | 0.3508 | 0.3508 | 2850630 |

这是 Debug smoke 数据，只证明工具链和采集路径可用；正式基线应使用 Release 构建和固定机器重复采集。

## 4. 后续扩展

下一批 R2 建议继续补齐：

- `BumpArena` / `ObjectPool` 分配微基准。
- SPSC mailbox 纯队列 enqueue/dequeue 微基准。
- Battle runtime 100 entities tick 成本微基准。
- 将 `architecture-acceptance-criteria.md` 中 Actor “未测定”项回填为 R2 数据链接。
