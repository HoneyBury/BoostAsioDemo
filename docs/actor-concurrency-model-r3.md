# R3 Actor 并发模型与线程边界

> 日期：`2026-05-16`  
> 范围：`ActorSystem`、`ActorRef::tell()`、跨 core mailbox、dispatch owner core、shutdown 顺序。

## 1. 当前结论

当前 Actor runtime 是 **owner-thread 调度模型**，不是任意线程可直接驱动 actor map 的 fully concurrent actor system。

允许跨 core 的路径是：

1. 发送方调用 `ActorRef::tell()`。
2. `ActorSystem::send()` 根据目标 actor affinity 判断是否跨 core。
3. 如果当前 IO core 与目标 core 不一致，消息只进入 `IoEngine::post_mailbox(target_core)`。
4. 目标 core 调用 `ActorSystem::drain_mailbox_and_dispatch(core_id)`。
5. `dispatch_owner_core()` 在 actor `on_message()` 期间暴露为本次 drain 的 `core_id`。

禁止跨线程直接共享的对象：

| 对象 | 边界 |
|---|---|
| `actors_` | 只能由 ActorSystem owner 调度路径访问 |
| `ready_actors_` | 只能在 dispatch/drain 阶段修改 |
| `scheduled_messages_` | 只能在 ActorSystem 调度路径修改 |
| actor 实例 | 只能在 `on_start` / `on_message` / `on_stop` 所属调度上下文访问 |

## 2. Owner Core 语义

| 调用路径 | `dispatch_owner_core()` |
|---|---|
| `dispatch_all()` 且 IO engine 有 current core | 当前 IO core |
| `dispatch_all()` 且没有 IO current core | `nullopt` |
| `drain_mailbox_and_dispatch(core_id)` | 传入的 `core_id` |
| dispatch 结束后 | 恢复为进入 dispatch 前的值 |
| shutdown 后 | `nullopt` |

这保证跨 core mailbox 被 drain 时，actor 能看到真实 owner core，而不是发送方 core。

## 3. Shutdown 顺序

当前 shutdown 顺序固定为：

1. 设置 `shutting_down_ = true`。
2. 清空 `dispatch_owner_core_`。
3. 清空 ready queue。
4. 清空 scheduled messages。
5. 对已启动 actor 调用 `cancel_all_owned_schedules()`。
6. 调用 actor `on_stop()`。
7. 清空 actor map。

dispatch 过程中如果 actor 调用 `shutdown()`，当前消息处理完成后立即停止 dispatch，避免继续访问已释放 actor cell。

## 4. 已固化测试

| 测试 | 覆盖 |
|---|---|
| `DispatchOwnerCoreReflectsCurrentIoCoreDuringDispatch` | 普通 dispatch 的 owner core |
| `DrainMailboxDispatchUsesDrainedCoreAsOwner` | 跨 core drain 使用目标 core |
| `ShutdownDuringDispatchStopsWithoutDeliveringQueuedTail` | dispatch 中 shutdown 不继续访问队尾消息 |
| `CrossCoreMailboxStressDoesNotDropMessages` | 10K cross-core tell 不丢消息 |
| `ShutdownDropsQueuedCrossCoreMailboxMessagesSafely` | shutdown 后 mailbox 中旧消息安全丢弃 |

## 5. 后续 R3 缺口

- 在 Debug/Test 模式增加更强 owner-thread 断言。
- 为真实 `AsioIoEngine` 增加跨线程 producer/consumer 压力测试。
- 把 gateway/backend shutdown 顺序和 ActorSystem shutdown 顺序统一到 runtime playbook。
