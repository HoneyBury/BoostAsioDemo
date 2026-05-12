# BoostGateway SDK 企业级重构规划

> 版本: v4.0.0 规划草案
> 目标: 将当前 SDK 重构为可独立分发的企业级客户端库

## 1. 现状评估

### 当前问题

| 问题 | 影响 |
|------|------|
| SDK 依赖服务端 `project_net` 库 | 客户端必须完整编译服务端项目 |
| 无 `find_package` 支持 | 客户端只能通过 `add_subdirectory` 集成 |
| 协议编解码耦合在 `net::packet` 中 | SDK 无法独立演进 |
| 无版本化 API | 破坏性变更无法管理 |
| 同步阻塞 API 为主 | 不适合高性能客户端 |
| 无连接池/重连策略 | 生产环境不可靠 |
| 无传输层抽象 | 绑死 TCP，无法扩展 WebSocket/QUIC |
| 线程安全不完整 | 回调可能竞态 |

### 当前 SDK 结构

```
sdk/
├── include/boost_gateway/sdk/
│   ├── client.h       SdkClient (单连接, 同步)
│   ├── types.h        基础类型
│   └── error.h        SDK 错误码
├── src/
│   ├── client.cpp     实现 (含 TcpConnection)
│   └── connection.cpp  (冗余)
├── tests/             GTest 单元测试
├── examples/          echo + full_flow
└── docs/              README + quickstart
```

## 2. 目标架构

```
┌─────────────────────────────────────────────────────┐
│                   Client Application                │
├─────────────────────────────────────────────────────┤
│  boost_gateway::sdk::Client  (高层 API)             │
│  ├── connect / login / room / battle / match        │
│  ├── async variants (callback + future)             │
│  └── event callbacks (push, disconnect, error)      │
├─────────────────────────────────────────────────────┤
│  boost_gateway::sdk::protocol  (协议层)             │
│  ├── PacketCodec (独立编解码, 零依赖)               │
│  ├── MessageRouter (请求/响应关联)                  │
│  └── ProtocolTypes (消息号 + 错误码)                │
├─────────────────────────────────────────────────────┤
│  boost_gateway::sdk::transport  (传输层)            │
│  ├── TcpTransport (Boost.Asio)                      │
│  ├── TransportInterface (抽象, 可替换)              │
│  └── ConnectionPool (连接池 + 重连)                 │
├─────────────────────────────────────────────────────┤
│  boost_gateway::sdk::internal  (内部工具)           │
│  ├── Logger (可注入的日志接口)                      │
│  ├── ThreadPool (异步回调调度)                      │
│  └── Timer (心跳/超时管理)                          │
└─────────────────────────────────────────────────────┘
```

### 依赖关系

```
SDK 外部依赖:
  Boost.Asio (header-only)  — 网络
  nlohmann_json (header-only) — JSON 解析

SDK 零依赖:
  - 不依赖 project_net (协议编解码独立实现)
  - 不依赖 project_v2 (服务端代码完全解耦)
  - 不依赖 project_app (日志独立接口)
```

## 3. 目录重构

```
sdk/
├── CMakeLists.txt                    # 顶层: 库 + 安装 + 导出
├── cmake/
│   └── boost_gateway_sdk-config.cmake.in  # find_package 配置模板
├── include/boost_gateway/sdk/
│   ├── client.h                      # Client (高层 API)
│   ├── types.h                       # 公共类型
│   ├── error.h                       # 错误码
│   ├── version.h.in                  # 版本号 (CMake 生成)
│   ├── protocol/
│   │   ├── codec.h                   # 协议编解码 (独立)
│   │   ├── message.h                 # 消息号定义
│   │   └── router.h                  # 请求/响应关联
│   └── transport/
│       ├── transport.h               # TransportInterface 抽象
│       ├── tcp_transport.h           # TCP 实现
│       └── connection_pool.h         # 连接池
├── src/
│   ├── client.cpp
│   ├── protocol/
│   │   ├── codec.cpp
│   │   └── router.cpp
│   └── transport/
│       ├── tcp_transport.cpp
│       └── connection_pool.cpp
├── tests/
│   ├── unit/
│   │   ├── codec_test.cpp
│   │   ├── router_test.cpp
│   │   ├── transport_test.cpp
│   │   ├── client_test.cpp
│   │   └── connection_pool_test.cpp
│   └── integration/
│       └── full_flow_test.cpp
├── examples/
│   ├── minimal/                      # 最小连接示例
│   ├── async/                        # 异步 API 示例
│   └── advanced/                     # 连接池+重连 示例
└── docs/
    ├── README.md                     # SDK 总览
    ├── quickstart.md                 # 5 分钟入门
    ├── api-reference.md              # API 参考
    ├── migration-v3-to-v4.md         # v3→v4 迁移指南
    └── roadmap.md                    # 本文档
```

## 4. API 设计

### 4.1 Client (同步)

```cpp
namespace boost_gateway::sdk {

class Client {
public:
    struct Config {
        std::string host = "127.0.0.1";
        uint16_t port = 9201;
        std::chrono::milliseconds connect_timeout{5000};
        std::chrono::milliseconds request_timeout{5000};
        uint32_t max_retries = 3;
        bool auto_reconnect = true;
        std::chrono::seconds heartbeat_interval{15};
    };

    explicit Client(Config config);

    // 连接管理
    bool connect();
    void disconnect();
    bool is_connected() const;

    // 认证
    LoginResult login(const std::string& user_id,
                      const std::string& token);

    // 房间
    RoomResult create_room(const std::string& room_id);
    RoomResult join_room(const std::string& room_id);
    RoomResult leave_room(const std::string& room_id);
    RoomResult set_ready(bool ready);

    // 战斗
    BattleStartResult start_battle(const std::string& room_id);
    BattleInputResult send_battle_input(const std::string& input);

    // 匹配
    MatchResult join_match(int64_t mmr, const std::string& mode);
    MatchResult leave_match(const std::string& mode);

    // 排行榜
    LeaderboardResult submit_score(int64_t score);
    LeaderboardResult get_top(int k);
    LeaderboardResult get_rank(const std::string& user_id);

    // 回调
    void on_push(PushCallback cb);
    void on_disconnect(DisconnectCallback cb);
    void on_error(ErrorCallback cb);
};

} // namespace
```

### 4.2 Client (异步)

```cpp
class AsyncClient {
public:
    // 返回 std::future<Result>
    std::future<LoginResult> login_async(...);
    std::future<RoomResult> create_room_async(...);

    // 回调版本
    void login_async(..., std::function<void(LoginResult)> callback);
};
```

### 4.3 Transport 抽象

```cpp
class ITransport {
public:
    virtual ~ITransport() = default;
    virtual bool connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;
    virtual bool send(const std::vector<uint8_t>& data) = 0;
    virtual std::vector<uint8_t> receive(std::chrono::milliseconds timeout) = 0;
    virtual void on_receive(ReceiveCallback cb) = 0;
};
```

## 5. 构建系统

### 5.1 CMake 导出

```cmake
# sdk/CMakeLists.txt
include(CMakePackageConfigHelpers)
include(GNUInstallDirs)

install(TARGETS boost_gateway_sdk
    EXPORT boost_gateway_sdk-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(EXPORT boost_gateway_sdk-targets
    FILE boost_gateway_sdk-targets.cmake
    NAMESPACE boost_gateway::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/boost_gateway_sdk
)

configure_package_config_file(
    cmake/boost_gateway_sdk-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/boost_gateway_sdk-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/boost_gateway_sdk
)
```

### 5.2 客户端使用

```cmake
# 客户端项目的 CMakeLists.txt
find_package(boost_gateway_sdk REQUIRED)
target_link_libraries(my_game PRIVATE boost_gateway::sdk)
```

或者通过 FetchContent:

```cmake
include(FetchContent)
FetchContent_Declare(boost_gateway_sdk
    GIT_REPOSITORY https://github.com/HoneyBury/BoostAsioDemo.git
    GIT_TAG v4.0.0
    SOURCE_SUBDIR sdk
)
FetchContent_MakeAvailable(boost_gateway_sdk)
target_link_libraries(my_game PRIVATE boost_gateway::sdk)
```

## 6. 实施阶段

### Phase S1: 依赖解耦 + 独立编解码 (基础)
- 从 `net::packet_codec.h` 提取协议编解码到 `sdk/include/.../protocol/codec.h`
- 移除对 `project_net` 的依赖
- SDK 完全自包含 (仅依赖 Boost.Asio + nlohmann_json)
- 测试: codec_test.cpp

### Phase S2: 传输层抽象 + 连接池
- `ITransport` 接口 + `TcpTransport` 实现
- `ConnectionPool` 支持多连接 + 自动重连
- 测试: transport_test.cpp, connection_pool_test.cpp

### Phase S3: 消息路由 + 异步 API
- `MessageRouter` 请求/响应关联 (基于 request_id)
- `AsyncClient` with `std::future` + callback
- 测试: router_test.cpp

### Phase S4: CMake 包导出 + 安装
- `find_package(boost_gateway_sdk)` 支持
- `cmake --install` 安装头文件 + 库 + CMake 配置
- 版本文件 `version.h.in`

### Phase S5: 文档 + 示例 + 迁移指南
- API 参考文档 (Doxygen 兼容)
- v3→v4 迁移指南
- 3 个示例: minimal / async / advanced

## 7. 版本兼容

| SDK 版本 | Gateway 版本 | API 兼容 |
|---------|-------------|---------|
| v2.4.0 | v2.x | 初始 |
| v3.0.0 | v3.x | 新增 match/leaderboard |
| v4.0.0 | v2.x+ | 破坏性变更 (独立包) |

## 8. 验收标准

- SDK 可独立编译 (不依赖服务端任何 .cpp)
- `find_package(boost_gateway_sdk)` 可用
- 异步 API P99 回调延迟 ≤ 1ms
- v3→v4 迁移 ≤ 5 处代码修改
- 文档覆盖全部公开 API
