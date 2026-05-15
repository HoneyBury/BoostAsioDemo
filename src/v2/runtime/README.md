# v2 Runtime Sources

当前目录主要包含：

- `actor_system.cpp`
  v2 Actor 运行时调度器、mailbox、延迟任务、周期任务、owned schedule 生命周期

当前主线状态：

- v2 runtime 仍是主业务流的本地调度核心
- `Gateway Runtime` 在 `include/src v2/gateway/runtime.*` 中承载更高层业务编排
- 当前阶段的变化重点不在 runtime 调度器结构本身，而在：
  - typed envelope 接线
  - schema / shadow bridge / service bridge 验证
  - 恢复/追平测试矩阵
