# AI 团队模式提示文档

本目录用于存放面向 AI 协作的标准提示文档，目标是在**严格遵守现有 `docs/` 规划文档**的前提下，开启项目级团队模式。

## 当前阶段说明

基于近期 `git log` 与现有 `v2` 文档，当前项目 `develop` 分支已经进入 **v2.0 实作阶段**，不再是“仅限 v1.x 维护”或“v2 只做启动骨架”的状态。

当前判断依据：

- 近期提交已连续落地 `M2`、`M4`、`M5`、`M6` 相关实现
- `docs/v2-startup-checklist.md` 已明确写明“当前 `develop` 已进入 `v2` 实作阶段”
- `docs/v2-roadmap.md`、`docs/v2-next-phases.md` 已记录全部七大模块（M1-M7）完成状态，315 单元测试 + 28 集成测试通过

因此，本目录中的 Agent 提示词默认应按以下方式理解项目状态：

1. `develop` 主线默认按 `v2` 任务处理。
2. `v1` 文档仍然是兼容链路、桥接边界、历史回归和现网语义的重要事实源。
3. 任何 `v2` 开发都必须区分“已落地能力”“当前进行中能力”“尚未进入实现的规划项”，不得把 roadmap 全量目标误当成现状。

## 文档列表

- [团队模式总控提示](./team-mode-orchestrator.md)
- [任务规划 Agent 提示词](./task-planning-agent.md)
- [子模块实现 Agent 提示词](./module-implementation-agent.md)
- [单元测试 Agent 提示词](./unit-test-agent.md)
- [集成测试 Agent 提示词](./integration-test-agent.md)

## 使用原则

1. 所有 Agent 都必须先读取 `docs/README.md`，再按任务需要读取对应事实源文档。
2. 文档冲突时，按以下优先级处理：
   - 与当前任务阶段直接对应的状态文档与事实源文档
   - `docs/v2-startup-checklist.md`
   - `docs/v2-roadmap.md`
   - `docs/v2-next-phases.md`
   - `docs/development-priority.md`
   - 对应专题事实源文档，例如 `v2-runtime.md`、`v2-protocol-bridge.md`、`v2-player-lifecycle.md`、`v2-room-lifecycle.md`、`v1-business-fact-source.md`、`v1-string-protocol.md`、`v1-runtime-lifecycle.md`
   - `docs/engineering-guide.md`
   - 其他说明性文档
3. 当前阶段默认是 `v2` 实作期；若任务涉及 `GatewayServer` 主链、旧协议兼容、回归护栏或维护分支语义，必须同时引用对应 `v1` 文档。
4. 任何实现、测试、回归修复都必须回链到明确文档依据、明确模块边界、明确验收标准。
5. 如果规划文档没有授权某项能力，Agent 不得自行脑补为“已实现”或“允许开发”；如果 roadmap 只是长期目标，Agent 也不得把它写成“当前已落地”。

## 推荐执行顺序

1. 使用《团队模式总控提示》启动整个多 Agent 协作。
2. 由《任务规划 Agent 提示词》产出任务拆解、优先级、模块边界与交付清单。
3. 每个子模块分别交给《子模块实现 Agent 提示词》执行。
4. 模块实现过程中同步触发《单元测试 Agent 提示词》补齐测试与验收。
5. 阶段完成后由《集成测试 Agent 提示词》完成端到端验收、回归和缺陷闭环。
