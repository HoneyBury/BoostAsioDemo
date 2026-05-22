# 坦克大战 Demo 规划

更新时间：2026-05-22

本文档定义多人在线 2D 坦克大战作为业务验证样例的规划。该 demo 的目标是验证实时服务框架承载具体业务的能力，不改变主线“企业级高性能实时服务框架”的定位。

## 1. 验证目标

坦克大战 demo 用于验证：

- 注册/登录/重连等 identity 能力。
- 房间列表、创建房间、加入房间、准备、房主操作等 lobby 能力。
- 多人实时输入、tick、snapshot、push、断线恢复等 realtime instance 能力。
- 战斗结算、幂等提交、排行榜查询等 leaderboard 能力。
- SDK 在真实业务客户端中的封装边界。
- demo 业务在压测、监控、恢复、回滚中的可观测性。

坦克大战 demo 不用于定义：

- 框架通用 battle 规则。
- SDK 公共业务方法。
- 默认生产容量上限。
- 框架唯一业务模型。

## 2. 业务流程

目标闭环：

```text
register/login
  -> list_rooms
  -> create_room / join_room
  -> ready
  -> start realtime instance
  -> tank input / snapshot push
  -> disconnect / reconnect / resume
  -> battle settlement
  -> leaderboard top/rank
  -> leave / exit
```

## 3. 框架能力依赖

| 流程 | 使用框架能力 | demo 业务责任 |
|---|---|---|
| 注册 | identity/register | 不写入坦克初始道具 |
| 登录 | identity/login/session | 业务 profile 自管 |
| 列房 | lobby/list_rooms | 展示房间玩法标签 |
| 创建房间 | lobby/create_room metadata | 填写坦克模式、地图 id |
| 加入房间 | lobby/join_room | 校验业务限制 |
| 准备 | lobby/ready | UI 和业务提示 |
| 开始游戏 | realtime instance lifecycle | 创建 tank simulation |
| 战斗输入 | input envelope | 解释 move/fire/use_item |
| 状态同步 | snapshot push | 编码 tank world delta |
| 断线重连 | session resume + instance resume | 恢复坦克视图状态 |
| 结算 | settlement hook | 计算击杀、胜负、分数 |
| 排行 | leaderboard submit/top/rank | 显示业务榜单 |

## 4. Demo 业务模块

建议目录：

```text
demo/games/tank_battle/
  README.md
  docs/rules.md
  docs/protocol.md
  docs/acceptance.md
  protocol/tank_messages.md
  server/tank_simulation/
  server/tank_plugin/
  client_sdk_adapter/
  tests/
  scripts/
```

业务模块：

| 模块 | 责任 |
|---|---|
| `tank_simulation` | world、tank、bullet、wall、spawn、movement、collision、damage |
| `tank_plugin` | 适配 realtime instance SPI |
| `tank_settlement` | 胜负、击杀、伤害、生存、最终分数 |
| `tank_anti_cheat` | 移速、射速、穿墙、非法命中校验 |
| `client_sdk_adapter` | 把业务命令编码为框架 `send_instance_input` |
| `verify_tank_battle_demo.py` | 独立 demo 端到端验证 |

## 5. 业务协议边界

框架 envelope：

```json
{
  "domain": "realtime_instance",
  "operation": "input",
  "instance_id": "room_001:battle_001",
  "payload_type": "tank.input",
  "payload": "{...}"
}
```

坦克业务 payload 示例：

```json
{
  "seq": 42,
  "actions": [
    {"type": "move", "x": 1, "y": 0},
    {"type": "fire", "direction": 90}
  ]
}
```

框架只路由和记录 envelope，不解释 `move`、`fire`、`direction`。

## 6. 结算模型

业务输出：

```json
{
  "battle_id": "battle_001",
  "room_id": "room_001",
  "reason": "time_limit",
  "players": [
    {
      "user_id": "alice",
      "display_name": "Alice",
      "kills": 3,
      "deaths": 1,
      "damage": 1200,
      "win": true,
      "score": 1300
    }
  ]
}
```

框架 leaderboard submit 只消费通用字段：

- `user_id`
- `display_name`
- `score`
- `idempotency_key`
- `source = tank_battle_settlement`
- `context`

得分公式属于 demo，不进入 leaderboard backend。

## 7. SDK 封装

公共 SDK 使用方式：

```cpp
client.login(user_id, token);
client.create_room(room_id, metadata);
client.set_ready(true);
client.start_instance(room_id);
client.send_instance_input(instance_id, "tank.input", payload);
client.leaderboard_rank(user_id);
```

demo adapter 可以提供：

```cpp
tank.move(dx, dy);
tank.fire(direction);
tank.use_item(slot);
```

这些方法只存在于 `demo/games/tank_battle/client_sdk_adapter/`，不得进入 `sdk/include/boost_gateway/sdk/client.h`。

## 8. 验收标准

### D0：文档与边界

- demo 目录和规则文档存在。
- 业务协议与框架 envelope 分离。
- SDK adapter 不修改公共 SDK。

### D1：最小业务闭环

- 2 个客户端注册/登录。
- 创建房间、加入房间、双方 ready。
- 启动实时实例。
- 发送 move/fire 输入。
- 收到 snapshot push。
- 手动结束战斗。
- 查询 leaderboard。

### D2：断线重连

- 战斗中客户端断线。
- reconnect window 内重新登录。
- 恢复 room 和 instance。
- 收到 resume snapshot。
- 继续发送输入或进入结算。

### D3：业务规则

- movement、bullet、collision、damage 有 deterministic unit tests。
- 非法移速、射速、穿墙输入被拒绝或审计。
- 胜负和得分规则可复现。

### D4：可靠性验证

- demo full-flow summary 输出。
- gateway/backend metrics 能看到 demo 流量。
- leaderboard 幂等结算可验证。
- backend down / reconnect / timeout 有明确错误。

### D5：性能验证

- demo 压测脚本独立于框架 baseline。
- 报告包含连接数、tick rate、snapshot rate、P50/P90/P99、错误率、CPU/RSS。
- 结论只描述 tank demo，不替代框架 release baseline。

## 9. 禁止事项

- 不在 gateway 写坦克规则。
- 不在 login 写坦克初始奖励。
- 不在 room 写坦克地图解释。
- 不在 leaderboard 写击杀得分公式。
- 不在公共 SDK 暴露坦克业务 API。
- 不把 tank demo gate 放入默认 production evidence gate。

