# 生产证据固定 Runner 配置说明

日期：2026-05-18

本文档用于收束 P2：把 P6 生产证据从本地手动命令推进到可归档的固定 runner 流水线。默认 PR/Release 仍保持有界 smoke，真实依赖、长项和容量数据必须在固定 runner 上显式开启。

## N0 Summary 契约

固定 runner 相关 summary 统一要求如下：

- 顶层使用 `summary_version=2`
- `overall_pass` 用于表示统一口径的通过/失败；`passed` 继续兼容历史脚本
- `failed_category` 用于区分 `preflight`、`build`、`specialized`、`stability`、`data_recovery`、`observability`、`release_baseline`、`configuration`
- `environment` 记录 `platform`、`python`、`host`
- `artifacts` 明确列出子 summary、性能 summary、性能报告等路径

GitHub Actions Step Summary 统一通过 `scripts/render_validation_summary.py` 渲染，避免不同 workflow 的输出格式继续漂移。

## Runner 标签

GitHub Actions 的 `production-evidence.yml` 使用 JSON 输入解析 runner：

| 场景 | `runner` 输入 | 必需能力 |
| --- | --- | --- |
| 有界默认证据 | `"ubuntu-latest"` 或 `["self-hosted","production-evidence"]` | CMake、Ninja、Python、可绑定本地端口 |
| Redis live | `["self-hosted","production-evidence","redis-live"]` | Redis `127.0.0.1:6379` 可达 |
| Operator kind | `["self-hosted","production-evidence","operator-kind"]` | Docker daemon、kind、kubectl、make 可用 |
| Release baseline | `["self-hosted","production-evidence","release-baseline"]` | 固定 CPU/OS、低后台噪声、Release 构建目录 |
| Full evidence | `["self-hosted","production-evidence","redis-live","operator-kind","release-baseline","observability"]` | 上述全部能力，且允许测试进程绑定 loopback 端口 |

`runner` 输入必须是合法 JSON。单个 label 用带引号的字符串，例如 `"ubuntu-latest"`；多个 label 用数组，例如 `["self-hosted","production-evidence"]`。

## Workflow 场景

| 场景 | 关键输入 | 产物 |
| --- | --- | --- |
| Bounded default | 所有 include 设为 `false`，`soak_profile=smoke` | `production-evidence-summary.json`、`p6-*-summary.json`、`p6-candidate-audit-summary.json` |
| Redis + kind | `include_redis_live=true`、`include_operator_kind=true` | 额外验证 Redis live 与 Operator kind smoke |
| Observability runtime | `include_observability_runtime=true` | `p2-observability-runtime-summary.json`、`gateway-observability-runtime-summary.json` |
| Release baseline | `configuration=Release`、`include_release_baseline=true`、`perf_repetitions=3` | `p6-release-baseline-summary.json`、`runtime/perf/release-baseline/**` |
| Capacity baseline | `include_capacity_baseline=true`、`perf_repetitions=3`、`step_timeout_seconds=1800` | capacity profile perf summary |

生产候选推荐先跑 Redis + kind + observability runtime，再在 release-baseline 固定机器上独立跑 Release baseline 和 capacity baseline。Full evidence 可以作为手动最终归档，但不建议频繁触发。

## 预检与失败归因

Workflow 在正式执行前运行：

```bash
python scripts/check_fixed_runner_environment.py \
  --profile production-evidence \
  --build-dir <build-dir> \
  --summary-path runtime/validation/fixed-runner-preflight-summary.json
```

当启用 Redis 或 kind 时，workflow 会自动追加 `--require-redis` 或 `--require-kind`。预检 summary 会记录：

- `passed`：环境是否满足当前输入。
- `checks[]`：命令、Redis TCP、kind cluster、构建目录形态。
- `warnings[]`：非阻断能力缺失，例如未安装 Ninja。
- `errors[]`：阻断项，例如 Redis 不可达、Docker/kind 不可用。

如果 `fixed-runner-preflight-summary.json` 失败，优先修 runner 环境；如果预检通过但 `production-evidence-summary.json` 失败，再按 `failed_category` / `failed_step` 修业务门禁。

## 归档标准

每次 P2 生产证据流水线必须归档：

- `runtime/validation/fixed-runner-preflight-summary.json`
- `runtime/validation/production-evidence-summary.json`
- `runtime/validation/p6-stability-soak-summary.json`
- `runtime/validation/p6-data-recovery-summary.json`
- `runtime/validation/p6-specialized-e2e-summary.json`
- `runtime/validation/p6-candidate-audit-summary.json`
- 启用性能时归档 `runtime/validation/p6-release-baseline-summary.json` 与 `runtime/perf/release-baseline/**`
- 启用 runtime observability 时归档 `runtime/validation/p2-observability-runtime-summary.json` 与 `runtime/validation/gateway-observability-runtime-summary.json`

通过标准是所有启用项 summary 的 `passed=true`。容量专项如果用于发现上限，可以允许性能 gate 失败，但必须在发布说明中标注它是容量边界证据，不得把失败 capacity 结果声明为生产基线通过。

## 推荐运行矩阵

| 阶段 | 建议运行项 | 目标 |
| --- | --- | --- |
| 每周例行 | bounded default + runtime observability | 确认默认生产证据链和 HTTP 观测没有回归 |
| Redis / kind 例行 | Redis live + Operator kind | 持续沉淀真实依赖场景证据 |
| 性能例行 | release baseline + capacity baseline | 沉淀 baseline/capacity 趋势和退化点 |
| 发布前 | Redis + kind + runtime observability + release baseline | 形成完整生产候选 evidence |
