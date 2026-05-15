# v2 Actor Sources

当前目录包含 v2 Actor 运行时的核心实现：

- `actor.cpp`
  Actor 生命周期、定时调度所有权、owned schedule 清理
- `actor_ref.cpp`
  `tell` / `schedule_after` / `schedule_every` 等对 `ActorSystem` 的薄封装

当前主线状态：

- `ActorSystem` 仍以单线程调度为主
- 已支持 wall-clock delay、dispatch delay、repeat schedule、owned schedule cancel
- `RemoteActorTransport` 在 v3 侧补了 typed envelope 编码，但本地 actor runtime 本身仍保持轻量
