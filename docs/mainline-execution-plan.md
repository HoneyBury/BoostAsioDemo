# Mainline Execution Plan

更新时间：2026-05-30

本文档是当前主线的执行计划，不是历史蓝图，也不是纯长期愿景。
它用于把“已经收口的事实源”推进到“固定 runner 可复现、默认依赖路径稳定、helper 兼容层可退场”的下一阶段。

如果本文件与 `current-state.md` 冲突：

- 已实现事实，以 `current-state.md` 和脚本结果为准
- 接下来 1-3 个月的执行顺序，以本文件为准

## 当前起点

当前已经成立的事实：

- 默认生产主链仍是 `SDK + TCP gateway + BackendEnvelope + 五后端 + Redis`
- `AdminService` 已明确留在 `legacy-v1` / demo-only 面
- `gateway_metrics_exporter` 已迁入 `v2::diagnostics`
- Windows 主线 Conan 路径已打通：
  - `with_grpc=False`
  - `with_sqlite=False`
  - lockfile + `conan install` 已验证通过
- Linux `nosqlite` lockfile 已落仓：
  - `conan/locks/linux-gcc-x64-release-nogrpc-nosqlite.lock`
- 固定 runner / production evidence / release-capacity 默认事实源已切到 Linux/Ubuntu labels

## 本阶段主题

把当前主线从“仓库内已收口”推进到“固定 runner 上可复现、可持续发布”。

一句话目标：

让 `nosqlite` Conan 路径、Ubuntu fixed-runner 证据链、helper 退场准备和 gRPC 证据边界同时进入可执行状态。

## 阶段 1：Ubuntu Fixed-Runner 落地

目标：

- 让 Ubuntu fixed-runner 成为 release/capacity/long-soak 的主事实源

任务：

1. 生成 Linux `nosqlite` lockfile  
   命令：
   ```bash
   python scripts/generate_conan_lock.py --profile conan/profiles/linux-gcc-x64 --build-type Release --without-sqlite
   ```

2. 验证 lockfile install  
   命令：
   ```bash
   conan install . \
     --profile:host conan/profiles/linux-gcc-x64 \
     --profile:build conan/profiles/linux-gcc-x64 \
     --lockfile conan/locks/linux-gcc-x64-release-nogrpc-nosqlite.lock \
     -o "&:with_grpc=False" \
     -o "&:with_sqlite=False" \
     --output-folder=build/conan-release \
     --build=missing \
     -s build_type=Release
   ```

3. 刷新 fixed-runner 证据  
   workflow：
   - `release-baseline.yml`
   - `long-soak-capacity.yml`
   - `production-evidence.yml`

4. 归档并检查：
   - `runtime/validation/release-baseline-summary.json`
   - `runtime/validation/long-soak-capacity-summary.json`
   - `runtime/validation/fixed-runner-release-capacity-summary.json`
   - `runtime/validation/production-evidence-summary.json`

退出条件：

- Linux `nosqlite` lockfile 真实存在
- Linux fixed-runner `conan install` 通过
- 至少 1 次 release baseline 与 1 次 long-soak/capacity summary 归档成功

## 阶段 2：Conan 默认入口收口

目标：

- 让 Conan 从“opt-in PoC”推进到“主线路径默认推荐值”

任务：

1. 统一文档和 workflow 默认值到：
   - `with_grpc=False`
   - `with_sqlite=False`

2. 在固定 runner workflow 中显式消费 lockfile：
   - `conan-validate.yml`
   - `release-baseline.yml`
   - `long-soak-capacity.yml`

3. 把 dependency cache key 与下列输入绑定：
   - `conanfile.py`
   - `conan/profiles/**`
   - `conan/remotes*.json`
   - `conan/locks/*.lock`

4. 保留 fallback，但明确边界：
   - PR/开发应优先 Conan
   - fallback 仅用于依赖缺失或固定 runner 外环境

退出条件：

- 文档、workflow、脚本全部以 `nosqlite` lockfile 为主线路径
- 至少一个固定 runner workflow 使用 lockfile 成功执行

## 阶段 3：Helper / Raw JSON 退场准备

目标：

- 把 helper/raw JSON 兼容层从“历史存在”推进到“有明确退场前提”

任务：

1. 保持五个服务域的 generated schema / typed contract 覆盖矩阵持续更新
2. 禁止新增任何 raw JSON-only 主业务 handler
3. 把剩余 raw JSON 面收缩到 room governance / control-plane 风格消息与内部 Raft raw JSON RPC
4. 对默认 full-flow 链路继续增加 schema-first 检查点
5. 把 legacy raw JSON 留给兼容测试，不再承载新功能

退出条件：

- 每个服务域都有 typed/generated contract 状态说明
- raw JSON 主业务新增面为 0
- 剩余 raw JSON 面仅限 room governance / control-plane 风格消息与内部 Raft RPC

## 阶段 4：gRPC 继续只做证据

目标：

- 保持 gRPC 作为实验层，不让它干扰默认主线

任务：

1. 继续补 full-flow contract coverage
2. 基于已真实化的 `grpc_vs_tcp_perf_test.cpp` 扩展到更多非登录路径
3. 把 gRPC gateway 从 callback stub 推进到 `GatewayServiceBridge` 驱动的真实 backend 路由
4. 在 Ubuntu fixed-runner 上验证 `BOOST_BUILD_GRPC=ON` 的构建稳定性
5. 不在未完成 streaming/SDK/TLS/RBAC/observability 证据前切默认链路

退出条件：

- `check_v3_grpc_poc_decision.py` 的证据不再依赖占位 benchmark
- 仍明确 `defer_default_transport`

## 阶段 5：发布面继续收口

目标：

- 让顶层 docs / install / release 只表达当前主线

任务：

1. 继续压缩 legacy 示例在主文档中的可见度
2. 保证顶层 docs 与 install 清单完全一致
3. 继续把非主线说明放到：
   - `docs/legacy-helper-inventory.md`
   - `docs/v2-control-plane-preplan.md`
   - `docs/archive/`

退出条件：

- `check_current_docs_install.py` 持续通过
- 顶层 docs 不把 legacy/demo/实验面误写成当前默认能力

## 2026-05-30 本轮收口

本轮把 1-5 执行项推进到以下状态：

1. Conan/SDK 依赖兼容：`project_boost_asio` 已统一承接 SDK 与 SDK tests 的 Boost.Asio 头文件路径；Conan Boost 目标优先使用 `Boost::headers`，避免误链 `boost::boost` 聚合库。
2. Ubuntu fixed-runner 入口：`conan-validate.yml`、`release-baseline.yml`、`long-soak-capacity.yml` 与 `production-evidence.yml` 均默认指向 `conan/locks/linux-gcc-x64-release-nogrpc-nosqlite.lock`。
3. Workflow lockfile 消费：`long-soak-capacity.yml` 已从 lockfile hint 升级为真实 `conan install` + Conan CMake configure/build 预检；治理入口为 `python3 scripts/check_conan_lockfile_workflows.py`。
4. helper/raw JSON 退场准备：`check_legacy_helper_inventory.py` 现在要求文档明确剩余 raw JSON 仅限 room governance / control-plane 风格消息与内部 Raft raw JSON RPC，并继续禁止新增 raw JSON-only 业务 handler。
5. gRPC 证据边界：`check_v3_grpc_poc_decision.py` 继续要求非登录路径证据作为下一步，同时保持 `defer_default_transport`。

仍不能在本地伪造的退出条件：Ubuntu fixed-runner 上真实执行 lockfile-based `conan install`、release baseline、long-soak/capacity 和 production evidence，并归档对应 summary。

本地治理允许用 `python3 scripts/check_validation_summary_contract.py --allow-missing` 验证 summary 契约形态；fixed-runner / 投产准入必须运行不带 `--allow-missing` 的严格检查，并要求真实 summary 存在。

## 当前明确不做

- 不把 `AdminService` 迁入主线
- 不把 `sqlite3` 作为 Conan 默认主线路径的一部分
- 不把 gRPC 接入默认生产链路
- 不扩 demo 业务面

## 执行顺序

1. Ubuntu fixed-runner 生成并消费 Linux `nosqlite` lockfile
2. 刷新 release/capacity/long-soak 固定 runner 证据
3. 将 Conan lockfile 绑定到更多固定 runner workflow
4. 补 helper/generated contract 覆盖矩阵
5. 继续保持 gRPC 为证据层，不切主链

## 通过判据

当以下条件同时满足时，本阶段可以认为完成：

1. Windows 与 Ubuntu `nosqlite` Conan lockfile 均存在
2. Windows 与 Ubuntu 的 lockfile-based `conan install` 均通过
3. Ubuntu fixed-runner 上有 release baseline 与 long-soak/capacity 真实 summary
4. helper/raw JSON 剩余面收缩到 room governance / control-plane 与内部 Raft RPC
5. 顶层 docs/install/release 面继续只表达当前默认主线
