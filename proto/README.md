# v3.0.0 Protocol Buffers 接口定义

本目录定义 BoostGateway v3.0.0 分布式运行时的服务间通信协议。

## 文件结构

```
proto/v3/
├── common.proto       — ServiceEnvelope 路由封装 + 所有 payload oneof
├── login.proto        — 登录认证服务
├── room.proto         — 房间管理服务
├── battle.proto       — 战斗模拟服务
├── match.proto        — 匹配队列服务
└── leaderboard.proto  — 排行榜服务
```

## 设计原则

1. **ServiceEnvelope 统一路由**: 所有服务间通信使用 `common.proto` 的 `ServiceEnvelope`，通过 `payload` oneof 区分消息类型
2. **向后兼容**: 字段编号不重用，新增字段追加到末尾
3. **W3C TraceContext**: `trace_id`/`span_id` 嵌入 Envelope 实现分布式追踪

## 编译

```bash
# 安装 protoc 和 grpc_cpp_plugin
# Ubuntu: apt install protobuf-compiler grpc-cpp-plugin
# macOS: brew install protobuf grpc

# 生成 C++ 代码
protoc --cpp_out=../src/v3/proto \
       --grpc_out=../src/v3/proto \
       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
       proto/v3/*.proto
```

## 消息流

```
Gateway ──ServiceEnvelope──▶ Login Backend
        ──ServiceEnvelope──▶ Room Backend
        ──ServiceEnvelope──▶ Battle Backend
        ──ServiceEnvelope──▶ Match Backend
        ──ServiceEnvelope──▶ Leaderboard Backend
```

## 与 v2.x JSON 兼容

v3.0.0 初期同时支持 Protobuf 和 JSON 序列化，通过 FeatureFlag `v3_protobuf_enabled` 灰度切换。
