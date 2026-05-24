# SDK 封装边界与演进说明

更新时间：2026-05-22

本文档定义 SDK 在实时系统框架中的职责。SDK 是框架客户端接入层，不是具体游戏业务 SDK。坦克大战或后续 demo 可以在 `demo/games/<demo>/client_sdk_adapter/` 提供业务薄封装，但不能把业务方法加入 `sdk/include/boost_gateway/sdk/client.h` 的公共框架 API。

## 1. SDK 目标

SDK 应稳定提供：

- 连接管理：`connect`、`disconnect`、`is_connected`。
- 心跳与断线：`start_heartbeat`、`stop_heartbeat`、`on_disconnect`。
- 认证：`login`，后续可扩展 `register_account`、`refresh_token`。
- lobby/room：`create_room`、`list_rooms`、`room_detail`、`join_room`、`leave_room`、`set_ready`。
- matchmaking：`match_join`、`match_leave`、`match_status`。
- realtime instance：`start_instance`、`send_instance_input`、`query_instance_state`、`resume_instance`。
- leaderboard：`leaderboard_submit`、`leaderboard_top`、`leaderboard_rank`。
- push 分发：session、room、match、instance、leaderboard 等通用 push。

当前已有 API 继续兼容；新增 API 应优先采用通用命名。现有 `start_battle` / `send_battle_input` 可作为兼容别名保留，但新增文档和新 demo 推荐使用 realtime instance 语义。

## 2. 公共 SDK 禁止项

以下内容不能进入框架 SDK：

- `tank_move`
- `tank_fire`
- `use_tank_item`
- `load_tank_map`
- `query_tank_hp`
- `submit_tank_kill_score`

这些属于业务 adapter，应该放到 demo 目录：

```text
demo/games/tank_battle/client_sdk_adapter/
```

## 3. 推荐分层

| 层级 | 位置 | 责任 |
|---|---|---|
| Framework SDK | `sdk/` | 通用连接、认证、room、match、instance、leaderboard |
| Demo Adapter | `demo/games/<demo>/client_sdk_adapter/` | 将业务命令编码为通用 input payload |
| Game Client | `demo/games/<demo>/client/` 或外部仓库 | UI、渲染、输入、本地预测、资源 |

示例：

```cpp
// Framework SDK
client.send_instance_input(instance_id, "tank.input", payload);

// Tank demo adapter
tank_client.fire(instance_id, direction);
```

`tank_client.fire()` 只是 adapter 里的便利方法，内部仍调用框架 SDK 的通用 input API。

## 4. 协议封装原则

公共 SDK 只理解框架 envelope：

- message id。
- request id。
- error code。
- service/domain。
- operation。
- payload bytes/string/json。
- timeout。

业务 payload 只做透传，不在框架 SDK 中解析成业务实体。

业务 adapter 可以解析：

- `TankInput`
- `TankSnapshot`
- `TankSettlement`
- `TankMap`

## 5. 错误模型

SDK 公共错误码保持通用：

- not connected。
- timeout。
- send failed。
- read failed。
- invalid response。
- auth required。
- backend unavailable。
- rate limited。
- schema invalid。
- instance not found。
- player not in instance。

业务错误由 payload 或业务 code 承载，例如：

- invalid tank command。
- fire cooldown。
- tank destroyed。
- map blocked。

公共 SDK 可以暴露业务错误 body，但不解释其含义。

## 6. Reconnect / Resume

框架 SDK 应提供通用重连流程：

1. heartbeat 或读写失败触发 `on_disconnect`。
2. 业务调度器决定是否重连。
3. `connect`。
4. `login` 或 `resume_session`。
5. `resume_instance` 获取通用 resume payload。
6. demo adapter 将 payload 转为业务状态。

禁止 SDK 公共层直接假设“重连后恢复坦克位置”。

## 7. 多语言封装

C ABI、Python、C# wrapper 必须遵守相同边界：

- 优先暴露框架通用 API。
- 业务 adapter 可在 demo 目录单独提供 Python/C# 示例。
- native version 校验、加载诊断、错误码映射仍属于框架 SDK。

## 8. 验收标准

SDK 新增能力进入主线前必须满足：

- C++ API 有文档说明。
- C ABI 是否需要暴露有明确结论。
- Python/C# wrapper 要么补齐，要么在兼容矩阵声明暂不支持。
- full-flow 或 package consumer gate 覆盖至少一条真实 gateway 路径。
- API 命名不绑定某个 demo。
- 回调线程、阻塞行为、超时语义和资源释放规则清楚。

demo adapter 的验收标准：

- 可独立构建或明确依赖 demo client。
- 不修改框架 SDK。
- 有业务 payload schema。
- 有至少一个端到端 demo flow。

