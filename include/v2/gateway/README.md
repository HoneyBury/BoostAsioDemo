# v2 Gateway

当前目录承载 v2/v3 主线上最关键的桥接层能力：

- `DemoServer`
  负责 v2 演示入口、自包含 `/health` / `/ready` / `/metrics` 管理口、
  backend config 装配、archive sink、FeatureFlags、SecurityPolicy
- `GatewayActor` / `Runtime`
  负责 `SessionAdapter -> GatewayCommand -> Player/Room/Battle Actor` 的主业务流
- `GatewayServiceBridge`
  负责 `login/room/battle/match/leaderboard` 多后端路由、静态配置回退、
  `ClusterRouter`、`OtlpExporter`、`BackendMetrics`
- `SchemaValidator`
  已接入 6 条桥接路径的 JSON Schema 校验
- `battle_protocol_codec` / `battle_wire_parser`
  负责 battle started/frame/settlement/finished/input push 等协议体编码与解析

当前实际状态：

- `match` / `leaderboard` 已接入 typed `ServiceEnvelope` helper 兼容层
- `login` / `room` / `battle` 已具备 typed message kind 支持
- shadow bridge 策略与 emit policy 已由配置驱动并被集成测试覆盖
