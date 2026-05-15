# v2 Room Sources

当前目录主要包含：

- `room_actor.cpp`
  房间成员、ready、owner 转移、battle active 生命周期
- `room_backend_service.cpp`
  room backend 进程实现，当前已兼容 legacy raw JSON 与 typed envelope payload

当前主线状态：

- `RoomActor` 本地链路保持稳定
- `room_backend_service` 已进入 typed `ServiceEnvelope` 兼容阶段
- room create / join / ready / leave / start battle 的 shadow bridge、schema、集成测试都已覆盖
