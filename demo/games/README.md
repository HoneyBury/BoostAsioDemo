# Demo Games And Realtime Systems

更新时间：2026-05-22

本目录用于放置基于实时服务框架的业务验证样例。这里的内容用于验证框架能力，不定义框架主线目标。

## 1. 定位

允许放入：

- 多人在线游戏 demo。
- 实时协作 demo。
- 高性能 gateway / push / middleware 验证 demo。
- IoT 或控制类实时系统验证 demo。
- demo 专属协议、simulation、客户端 adapter、资源和验证脚本。

不允许放入：

- 框架核心 runtime。
- 通用 SDK 公共 API。
- 生产部署默认 gate。
- 通用 login/room/match/leaderboard 的唯一实现。

## 2. 目录规范

每个 demo 使用独立目录：

```text
demo/games/<demo_name>/
  README.md
  docs/
  protocol/
  server/
  client_sdk_adapter/
  client/
  tests/
  scripts/
```

目录职责：

| 目录 | 责任 |
|---|---|
| `docs/` | 业务规则、架构、验收说明 |
| `protocol/` | 业务 payload schema，不定义框架 envelope |
| `server/` | 业务 simulation、rule、adapter |
| `client_sdk_adapter/` | 基于框架 SDK 的业务薄封装 |
| `client/` | demo 客户端或测试客户端 |
| `tests/` | 业务单测和 demo 集成测试 |
| `scripts/` | 独立 demo 验证入口 |

## 3. 接入规则

demo 必须通过框架通用能力接入：

- identity/login。
- lobby/room。
- matchmaking。
- realtime instance input/snapshot。
- leaderboard。
- SDK connect/login/room/input/push。

demo 不能要求框架层新增业务专属 API。确实需要新增通用框架能力时，必须先更新：

- `docs/realtime-framework-modernization-plan.md`
- `docs/realtime-framework-module-boundaries.md`
- `docs/realtime-framework-sdk-boundary.md`

## 4. 验证规则

demo 可以有自己的验证脚本，例如：

```bash
python3 demo/games/tank_battle/scripts/verify_tank_battle_demo.py
```

demo 验证报告必须说明：

- 使用的框架版本。
- 使用的 SDK 版本。
- 启用的 backend profile。
- 是否依赖 Redis/TLS/K8s/OTel。
- 验证了哪些框架能力。
- 哪些结论只对 demo 成立。

## 5. 与生产 Gate 的关系

默认生产 gate 不依赖 demo。

demo gate 可以被固定 runner 或专项 workflow 显式启用，但不能自动替代：

- release candidate gate。
- production evidence gate。
- SDK enterprise delivery gate。
- transport/config governance gate。
- production readiness report。

如果某个 demo 暴露框架缺陷，应在框架层补通用能力和通用测试，而不是把 demo 变成默认生产依赖。

