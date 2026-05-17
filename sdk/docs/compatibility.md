# SDK 与 Gateway 兼容矩阵

更新时间：2026-05-17

本文档记录当前客户端 SDK 与 BoostGateway 服务端的生产接入口径。默认事实源以 SDK native 版本、C ABI、语言封装和真实 gateway full-flow gate 为准。

## 当前兼容线

| Gateway 版本 | SDK native 版本 | C ABI | Python wrapper | C# wrapper | 状态 |
| --- | --- | --- | --- | --- | --- |
| `v3.3.2` | `v4.1.0` | `gsdk_version()` 主版本 `4.x` | 校验 native 主版本 `4.x` | 校验 native 主版本 `4.x` | stable |

## 运行时校验

- C ABI 暴露 `gsdk_version()`，语言封装必须在创建 client 前校验 native 主版本。
- Python wrapper 支持 `BOOST_GATEWAY_SDK_LIBRARY` 指定 native library 路径；加载失败时输出尝试路径和底层错误。
- C# wrapper 在 native client allocation 失败时抛出明确异常。
- SDK full-flow gate 必须覆盖 login、echo、room、ready、battle、push、reconnect、heartbeat 和 disconnect callback。

## 兼容边界

- 当前 SDK 面向现有 TCP wire protocol，不承诺 proto/gRPC 外部客户端协议。
- `on_disconnect` 当前由 heartbeat failure 触发；主动 `disconnect()` 不触发该回调。
- `on_push` 回调在同步请求或 heartbeat 读到 push 时触发，回调内不应阻塞或递归调用同一个 client 的同步 API。
- 兼容升级默认策略：Gateway patch/minor 版本保持 SDK `4.x` 主版本兼容；破坏性协议变化必须提升 SDK 主版本并更新本矩阵。

## 示例入口

- C++：`sdk/examples/full_flow_client/main.cpp`
- Python：`sdk/examples/python_full_flow.py`
- C#：`sdk/examples/csharp_full_flow/Program.cs`
