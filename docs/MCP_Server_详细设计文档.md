# 集群聊天服务器 — MCP Server 技术设计与实现

## 目录

1. [背景与需求](#1-背景与需求)
2. [MCP 协议简介](#2-mcp-协议简介)
3. [整体架构设计](#3-整体架构设计)
4. [代码实现详解](#4-代码实现详解)
5. [数据库层扩展](#5-数据库层扩展)
6. [线程安全与并发控制](#6-线程安全与并发控制)
7. [部署与配置](#7-部署与配置)
8. [技术难点与解决方案](#8-技术难点与解决方案)
9. [面试问答准备](#9-面试问答准备)

---

## 1. 背景与需求

### 1.1 项目背景

集群聊天服务器是一个基于 muduo 网络库开发的 C++ 分布式即时通讯系统，支持多服务器集群部署、Kafka 跨服消息广播、MySQL 主从读写分离、Redis 缓存加速。

### 1.2 为什么需要 MCP Server

传统运维和日常使用中，如果我想：

- 查询当前有哪些用户在线
- 查看某个群组的成员列表
- 快速给某人发一条消息

我需要打开客户端 → 登录 → 一步步操作菜单，非常低效。

**核心需求**：让 AI Agent（如 opencode / Claude / ChatGPT）能够直接操控聊天服务器，用户只需要说一句话："帮我给张三发一条消息"，AI 就能自动完成全部操作。

### 1.3 MCP 的价值

MCP（Model Context Protocol）是 Anthropic 发布的开放标准，专为解决 AI 模型与外部工具交互的问题而设计。通过实现 MCP 协议，我们的聊天服务器成为了一个可以被任何兼容 MCP 的 AI 客户端调用的"工具集合"。

```
传统方式：用户 → 打字 → TCP客户端 → 聊天服务器
MCP 方式：用户 → 自然语言 → AI Agent → HTTP(MCP协议) → 聊天服务器
```

---

## 2. MCP 协议简介

### 2.1 协议架构

MCP 协议采用 C/S 架构，定义了三个核心概念：

| 概念 | 说明 | 本项目实现 |
|------|------|-----------|
| **Tool** | 服务器暴露的可调用功能 | 8 个工具（登录、发消息、查用户、查好友等） |
| **Resource** | 服务器提供的可读数据 | 本项目未使用（所有数据通过 Tool 返回） |
| **Prompt** | 预定义的提示模板 | 本项目未使用 |

### 2.2 通信流程

```
1. AI Client 发起 HTTP 请求到 /mcp 端点
   ↓
2. 服务器返回 SSE (Server-Sent Events) 流
   ↓
3. 客户端通过 JSON-RPC 2.0 协议调用工具
   ↓
4. 服务器执行工具逻辑，返回 JSON 结果
```

### 2.3 传输层选择

MCP 支持两种传输方式：

- **Stdio**：通过标准输入/输出通信，适用于本地进程
- **HTTP + SSE**：通过 HTTP 请求/响应 + Server-Sent Events 通信，适合远程访问

本项目选择 **HTTP + SSE** 模式，原因：

1. 聊天服务器是长期运行的后台服务，不适合 stdio 模式
2. HTTP 模式支持多个 AI 客户端同时连接
3. SSE 模式提供流式响应，适合长时间的服务器监控

### 2.4 协议版本

实现的是 MCP 规范 **2025-03-26** 版本（Streamable HTTP）。

---

## 3. 整体架构设计

### 3.1 分层架构图

```
┌─────────────────────────────────────────────────────────┐
│                     AI Agent (opencode / Claude)         │
│                    发送自然语言指令                         │
└────────────────────────┬────────────────────────────────┘
                         │ HTTP SSE (JSON-RPC 2.0)
                         ▼
┌─────────────────────────────────────────────────────────┐
│              ChatMcpServer (mcp/chat_mcp_server.cpp)     │
│  ┌─────────────────────────────────────────────────┐    │
│  │  注册 8 个 Tool 及其 handler 函数                 │    │
│  │  - chat_user_login      验证身份                 │    │
│  │  - chat_send_message    发送私聊消息              │    │
│  │  - chat_server_stats    服务器状态统计            │    │
│  │  - chat_list_online_users 在线用户列表            │    │
│  │  - chat_get_user_info   查询用户信息              │    │
│  │  - chat_get_user_friends  查询好友列表            │    │
│  │  - chat_get_group_info  查询群组详情              │    │
│  │  - chat_list_user_groups  查询用户群组列表         │    │
│  └─────────────────────────────────────────────────┘    │
└────────────────────────┬────────────────────────────────┘
                         │ 调用 ChatService 单例
                         ▼
┌─────────────────────────────────────────────────────────┐
│                ChatService (chatservice.cpp)             │
│  ┌─────────────────────────────────────────────────┐    │
│  │  sendMessageByMcp() — MCP 专用消息投递            │    │
│  │  getOnlineUserIds() — 线程安全获取在线用户 ID      │    │
│  │  getConnectionCount() — 线程安全获取连接数         │    │
│  └─────────────────────────────────────────────────┘    │
└────────────────────────┬────────────────────────────────┘
                         │ 数据访问
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
     UserModel      FriendModel    GroupModel
          │              │              │
          ▼              ▼              ▼
    DatabaseRouter (主从读写分离)
          │
    ┌─────┴─────┐
    ▼           ▼
  MySQL主     MySQL从
```

### 3.2 线程模型

```
┌──────────────────────────────────┐
│   Main Thread (muduo EventLoop)  │
│   - TCP 连接管理                  │
│   - 聊天消息收发                  │
│   - Kafka 消费回调                │
└──────────────┬───────────────────┘
               │ 共享 _userConnMap (mutex 保护)
┌──────────────┴───────────────────┐
│  MCP Thread (c++_mcp HTTP Server) │
│  - 接收 HTTP 请求                 │
│  - 执行 Tool handler 回调         │
│  - 返回 JSON 响应                 │
└──────────────────────────────────┘
```

**关键决策**：MCP 服务器运行在独立线程上。这是必要的，因为：

1. MCP 使用阻塞式 HTTP 服务器，不能放在 muduo 的 Reactor 事件循环中
2. 两个线程通过 `std::mutex (_connMutex)` 保护共享数据 `_userConnMap`
3. 数据库操作各自从连接池获取独立连接，不需要额外同步

### 3.3 MCP Server 与 Chat Server 的关系

```
./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888

启动后同时运行两个服务器：
┌─────────────────┐     ┌──────────────────┐
│  Chat TCP Server │     │  MCP HTTP Server  │
│  监听 0.0.0.0:6000│     │  监听 0.0.0.0:8888│
│  (muduo Reactor) │     │  (c++_mcp 线程池) │
└────────┬────────┘     └────────┬─────────┘
         │                       │
         └───────┬───────────────┘
                 ▼
          ChatService 单例
        (共享业务逻辑 + 数据)
```

MCP 不是替代 Chat Server，而是它的**补充**。Chat Server 处理 TCP 客户端的实时聊天，MCP Server 提供 HTTP API 供 AI Agent 调用。两者共享同一套业务逻辑和数据库连接。

---

## 4. 代码实现详解

### 4.1 第三方库：c++_mcp

**选型理由**：c++_mcp 是目前最成熟的 C++ MCP 协议实现库，支持 HTTP + SSE 传输，提供了完整的 Tool 注册、JSON-RPC 解析、会话管理等功能。

**引入方式**：将 c++_mcp 源码（约 15 个 .cpp 文件）直接放入项目的 `c++_mcp/` 子目录，通过 CMake 编译进最终的二进制文件。

**CMakeLists.txt 片段**：
```cmake
# 主 CMakeLists.txt
set(CMAKE_CXX_STANDARD 17)  # c++_mcp 需要 C++17

# src/server/CMakeLists.txt
file(GLOB MCP_SOURCES "c++_mcp/src/*.cpp")
add_executable(ChatServer ${SOURCES} ${MCP_SOURCES})
target_link_libraries(ChatServer c++_mcp muduo_net muduo_base ...)
```

### 4.2 ChatMcpServer 单例包装器

**文件**：`src/server/mcp/chat_mcp_server.h` / `.cpp`

**为什么需要包装器**：c++_mcp 是一个通用库，不感知我们的业务逻辑。`ChatMcpServer` 负责：
1. 将 c++_mcp 的 Server 配置为适合我们项目的参数
2. 注册我们业务特定的 8 个 Tool
3. 提供 `start(port)` / `stop()` 生命周期管理

**核心启动流程**（`chat_mcp_server.cpp:24-53`）：

```cpp
bool ChatMcpServer::start(uint16_t port) {
    // 第1步：配置 HTTP 服务器参数
    mcp::server::configuration config;
    config.host = "0.0.0.0";         // 监听所有网卡
    config.port = port;               // 从 --mcp-port 参数传入
    config.thread_pool_min_size = 1;  // 最少1个工作线程
    config.thread_pool_max_size = 4;  // 最多4个工作线程（适合轻量查询）
    config.max_sessions = 10;         // 最多10个并发AI客户端
    config.session_timeout = 60s;     // 会话60秒无活动自动回收

    // 第2步：创建服务器实例
    server_ = std::make_unique<mcp::server>(config);

    // 第3步：设置服务器元信息（协议握手时发送给客户端）
    server_->set_server_info("ChatClusterServer", "1.0.0");
    server_->set_instructions(
        "This MCP server provides monitoring, management, ..."
    );

    // 第4步：注册所有工具
    registerTools();

    // 第5步：启动 HTTP 服务器（false = 非阻塞，在后台线程运行）
    server_->start(false);
}
```

**设计模式**：使用**单例模式**（`instance()` 方法），确保整个进程只有一个 MCP Server 实例。

### 4.3 Tool 注册机制

每个 Tool 的注册遵循统一的三步模式：

```cpp
// 第1步：用 tool_builder 构建工具描述（声明名称、描述、参数）
server_->register_tool(
    mcp::tool_builder("chat_send_message")             // 工具名
        .with_description("Send a private message...")  // 工具描述（AI 用此理解工具用途）
        .with_number_param("from_user_id", "...", true) // 参数定义（类型+是否必填）
        .with_number_param("to_user_id", "...", true)
        .with_string_param("message", "...", true)
        .build(),                                       // 构建完成

    // 第2步：提供 handler 回调函数
    [svc](const json& params, const string&) -> json {
        // 第3步：从 params 提取参数，执行业务逻辑，返回 JSON 结果
        int fromId = params["from_user_id"].get<int>();
        // ...
        return {{"success", true}, {"message", "Message sent successfully"}};
    }
);
```

**关键设计点**：

1. **handler 是 C++ lambda**：捕获 `ChatService::instance()` 指针（`[svc]`），每个 handler 都是一个独立的 lambda 表达式
2. **参数校验在 handler 内部**：先检查用户是否存在、消息是否为空等，再执行业务逻辑
3. **返回值是 nlohmann::ordered_json**：AI 客户端收到 JSON 后解析展示给用户

### 4.4 chat_user_login（账号登录）

**完整流程**：

```
AI 调用: chat_user_login(user_id=1, password="123")
    │
    ├─ [1] 从主库查询用户（forceMaster=true，避免主从延迟）
    │      User user = svc->getUserModel().query(userId, true);
    │
    ├─ [2] 验证用户是否存在
    │      不存在 → 返回 {"success": false, "error": "User not found"}
    │
    ├─ [3] 验证密码
    │      密码错误 → 返回 {"success": false, "error": "Invalid password"}
    │
    ├─ [4] 检查是否已被 TCP 客户端登录
    │      state == "online" → 返回 {"success": false, "error": "Already logged in"}
    │      （防止 MCP 登录与真实客户端冲突）
    │
    ├─ [5] 查询好友列表
    │      vector<User> friends = svc->getFriendModel().query(userId);
    │
    ├─ [6] 查询群组列表
    │      vector<Group> groups = svc->getGroupModel().queryGroups(userId);
    │
    └─ [7] 返回完整用户信息
           {
             "success": true,
             "userName": "alice",
             "userId": 1,
             "friends": [{"id": 2, "name": "bob"}, ...],
             "groups": [{"id": 1, "name": "技术交流群"}, ...]
           }
```

**为什么 MCP 登录不修改 state**：如果修改 state 为 "online"，用户将无法通过真实 TCP 客户端登录，因为系统认为"已经在线"。MCP 登录只是身份验证，不建立持久"在线"状态。

### 4.5 chat_send_message（发送消息）

**完整流程**：

```
AI 调用: chat_send_message(from_user_id=1, to_user_id=2, message="你好")
    │
    ├─ [1] 参数校验
    │      - 消息不能为空
    │      - from_user_id != to_user_id（不能自己给自己发）
    │
    ├─ [2] 验证发送方和接收方用户存在
    │      任一不存在 → 返回错误
    │
    ├─ [3] 调用 ChatService::sendMessageByMcp(fromId, toId, message)
    │      │
    │      ├─ 构建 OneChatMessage Protobuf（与原生客户端格式完全一致）
    │      │   chat::OneChatMessage chatMsg;
    │      │   chatMsg.mutable_base()->set_msgid(chat::ONE_CHAT_MSG);
    │      │   chatMsg.mutable_base()->set_fromid(fromId);
    │      │   chatMsg.mutable_base()->set_toid(toId);
    │      │   chatMsg.mutable_base()->set_time(Timestamp::now());
    │      │   chatMsg.set_message(messageContent);
    │      │
    │      ├─ [检查1] 目标用户是否在本机的 _userConnMap 中？
    │      │   是 → it->second->send(serializedMsg);  ✅ 直接推送
    │      │
    │      ├─ [检查2] 目标用户是否在线（但不在本机）？
    │      │   是 → _kafkaManager->sendMessage("user_messages", serializedMsg);
    │      │        ✅ 通过 Kafka 广播给所有服务器
    │      │
    │      └─ [检查3] 目标用户不在线？
    │           是 → _offlineMsgModel.insert(toId, serializedMsg);
    │               ✅ 存入离线消息表，下次登录推送
    │
    └─ [4] 返回结果
           {
             "success": true,
             "from": {"id": 1, "name": "alice"},
             "to": {"id": 2, "name": "bob"},
             "deliveryMethod": "direct"  或  "offline_stored"
           }
```

### 4.6 chat_server_stats（服务器统计）

```
调用 → getConnectionCount() + getOnlineUserIds()
返回 → {"connections": 42, "onlineUsers": 35, "serverInfo": "v1.0.0"}
```

### 4.7 chat_list_online_users（在线用户列表）

```
调用 → getOnlineUserIds() → 遍历每个 ID → UserModel.query(id)
返回 → {"onlineUsers": [{"id": 1, "name": "alice"}, ...], "count": 35}
```

### 4.8 chat_get_user_info（用户信息查询）

```
调用 → UserModel.query(userId) → 查 MySQL/Redis 缓存
返回 → {"user": {"id": 1, "name": "alice", "state": "online"}, "isOnline": true}
```

### 4.9 chat_get_user_friends（好友列表查询）

```
调用 → UserModel.query(userId) + FriendModel.query(userId)
返回 → {"friends": [{"id": 2, "name": "bob"}, ...], "count": 10}
```

### 4.10 chat_get_group_info（群组信息查询）

```
调用 → GroupModel.queryGroup(groupId)
返回 → {"groupId": 1, "groupName": "技术群", "members": [...], "memberCount": 50}
```

### 4.11 chat_list_user_groups（用户群组列表）

```
调用 → GroupModel.queryGroups(userId)
返回 → {"groups": [{"id": 1, "name": "技术群", "memberCount": 50}, ...], "count": 3}
```

---

## 5. 数据库层扩展

### 5.1 新增：UserModel::queryByName

**为什么需要**：现有的 `UserModel::query(id)` 只能用数字 ID 查询用户，但 AI 用户倾向于使用"用户名"而非"ID"。`queryByName` 填补了这个空缺。

**实现位置**：`src/server/model/usermodel.cpp:88-125`

**关键代码**：
```cpp
User UserModel::queryByName(const string& name) {
    // 1. 从主库获取连接（forceMaster=true，保证一致性）
    auto conn = DatabaseRouter::instance()->routeQuery(true);

    // 2. SQL 注入防护：使用 mysql_real_escape_string 转义用户输入
    char name_escaped[256];
    mysql_real_escape_string(conn->getConnection(), name_escaped,
                             name.c_str(), name.length());

    // 3. 执行精确匹配查询
    char sql[1024];
    sprintf(sql, "select * from user where name = '%s'", name_escaped);

    // 4. 解析结果并写入缓存
    MYSQL_RES *res = conn->query(sql);
    if (res) {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row) {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setPwd(row[2]);
            user.setState(row[3]);
            mysql_free_result(res);

            // 5. 写入 Redis 缓存（减少后续查询的数据库压力）
            _cacheManager->cacheUser(user);

            return user;
        }
        mysql_free_result(res);
    }
    return User();  // 未找到返回默认值 (id=-1)
}
```

**安全措施**：
- `mysql_real_escape_string` 防止 SQL 注入
- 从主库读取（`routeQuery(true)`）避免主从延迟
- 查询结果写入 Redis 缓存加速后续访问

### 5.2 MCP 消息投递通路

`sendMessageByMcp` 与原生客户端 `oneChat` 使用完全相同的三层消息路由：

```
                    消息到达 sendMessageByMcp
                             │
                ┌────────────┴────────────┐
                ▼                         ▼
     目标在本机 _userConnMap？        目标不在本机
                │                         │
         ┌──────┴──────┐          ┌──────┴──────┐
         ▼             ▼          ▼             ▼
    是→直接TCP推送  否→继续    在线?         离线?
                                   │             │
                              ┌────┴────┐   ┌───┴───┐
                              ▼         ▼   ▼
                          Kafka广播  (跳过) 存储到DB
```

此通路保证了 MCP 发送的消息与 TCP 客户端发送的消息在投递行为上完全一致。

---

## 6. 线程安全与并发控制

### 6.1 共享资源分析

MCP 线程与主线程（muduo 事件循环）共享以下资源：

| 资源 | 访问方式 | 保护机制 |
|------|---------|----------|
| `_userConnMap` | MCP 读 / 主线程读写 | `std::mutex _connMutex` |
| `UserModel` / `FriendModel` / `GroupModel` | 两线程都读 | 各自从连接池获取独立 MySQL 连接 |
| `KafkaManager` | MCP 写 / 主线程读 | KafkaManager 内部自带 mutex |
| `OfflineMsgModel` | MCP 写 | 获取独立连接，无竞争 |

### 6.2 _userConnMap 的并发保护

```cpp
// 主线程：用户登录 → 写入 _userConnMap
void ChatService::login(...) {
    {
        lock_guard<mutex> lock(_connMutex);  // 锁定
        _userConnMap.insert({id, conn});     // 写入
    }  // 自动解锁
}

// MCP 线程：sendMessageByMcp → 读取 _userConnMap
bool ChatService::sendMessageByMcp(int fromId, int toId, ...) {
    {
        lock_guard<mutex> lock(_connMutex);  // 锁定
        auto it = _userConnMap.find(toId);   // 读取
        if (it != _userConnMap.end()) {
            it->second->send(serializedMsg);  // 发送消息
            return true;
        }
    }  // 自动解锁
    // ...
}
```

### 6.3 TcpConnection::send 的线程安全性

muduo 库的 `TcpConnection::send()` 是**多线程安全**的。它的内部实现会将实际的数据发送封装成一个任务，通过 `EventLoop::runInLoop()` 调度到 IO 线程执行。因此 MCP 线程可以直接调用 `send()` 不会有竞争条件。

### 6.4 连接池的并发处理

```cpp
// 每个线程从连接池获取独立连接
auto conn = DatabaseRouter::instance()->routeQuery();
// ... 使用 conn 执行查询 ...
DatabaseRouter::instance()->returnConnection(conn);
```

`ConnectionPool` 使用信号量（semaphore）管理可用连接，支持多线程并发获取和归还连接。每个线程拿到的是独立的 MySQL 连接，不需要在应用层做额外同步。

---

## 7. 部署与配置

### 7.1 启动参数

```bash
# 基本用法
./bin/ChatServer <IP> <TCP端口> [--mcp-port <MCP端口>]

# 示例：同时启动聊天服务器(6000)和MCP服务(8888)
./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888

# 不使用 MCP（向后兼容）
./bin/ChatServer 0.0.0.0 6000
```

### 7.2 Docker 部署

**docker-compose.yml 端口映射**：
```yaml
chat-server1:
  ports:
    - "6000:6000"    # TCP 聊天端口
    - "8888:8888"    # MCP HTTP 端口
  command: ["./bin/ChatServer", "0.0.0.0", "6000", "--mcp-port", "8888"]

chat-server2:
  ports:
    - "6001:6001"
    - "8889:8889"
  command: ["./bin/ChatServer", "0.0.0.0", "6001", "--mcp-port", "8889"]

chat-server3:
  ports:
    - "6002:6002"
    - "8890:8890"
  command: ["./bin/ChatServer", "0.0.0.0", "6002", "--mcp-port", "8890"]
```

### 7.3 AI 客户端连接

支持的 AI 客户端：

- **opencode**：在项目根目录的 `CLAUDE.md` 中声明 MCP Server 地址
- **Claude Desktop**：在配置文件中添加 MCP Server 地址
- **任意 MCP 兼容客户端**：连接 `http://<host>:8888/mcp`

---

## 8. 技术难点与解决方案

### 8.1 难点一：MCP 登录 vs TCP 登录的状态冲突

**问题**：如果 MCP 登录后把用户 state 改为 "online"，当用户尝试用真实客户端登录时，系统会返回"该账号已在使用中"。

**解决**：MCP 的 `chat_user_login` 工具**只验证身份**，不修改数据库中的用户状态。它返回用户信息和好友列表，但不将用户插入 `_userConnMap`，不修改 `state` 字段。

**权衡**：MCP 登录的用户在系统中不显示为"在线"，这避免了状态冲突但也意味着 MCP 用户无法接收实时消息推送。

### 8.2 难点二：线程模型冲突

**问题**：MCP HTTP 服务器（c++_mcp）使用阻塞式 IO 和线程池，而主聊天服务器使用 muduo 的 Reactor（非阻塞事件驱动）模式。

**解决**：
- MCP 服务器在**独立的线程**中运行（`server_->start(false)` 中的 `false` 参数表示非阻塞启动，后台运行）
- 共享数据通过 `std::mutex` 保护
- 数据库操作通过连接池获取独立连接

```cpp
// main.cpp 中的启动顺序
int main(int argc, char* argv[]) {
    // 1. 解析 --mcp-port 参数
    // 2. 启动 Chat TCP Server
    ChatServer server(&loop, listenAddr, serverName);
    server.start();

    // 3. 启动 MCP HTTP Server（独立线程）
    if (mcpPort > 0) {
        ChatMcpServer::instance()->start(mcpPort);
    }

    // 4. 运行主事件循环
    loop.loop();

    // 5. 清理
    ChatMcpServer::instance()->stop();
}
```

### 8.3 难点三：跨服务器消息路由

**问题**：MCP 连接的是 Server 1，但目标用户可能连接在 Server 2 或 3 上。如何确保消息正确投递？

**解决**：复用现有的 Kafka 跨服消息广播机制：

```
MCP Server 1 调用 sendMessageByMcp(fromId=1, toId=2, msg)
  ├── 目标 2 在 Server 1 的 _userConnMap 中？
  │    是 → 直接 TCP 推送  ✅
  │    否 → 查询数据库得知 2 的 state = "online"
  │          → 通过 Kafka topic "user_messages" 广播  ✅
  │              ├── Server 1 的 KafkaConsumer 收到
  │              ├── Server 2 的 KafkaConsumer 收到 → 找到用户 2 → 推送
  │              └── Server 3 的 KafkaConsumer 收到 → 没找到用户 2 → 忽略
  └── 2 不在线 → 存入 offlinemessage 表  ✅
```

**关键**：所有服务器使用不同的 `group.id`（如 `chat_server_group_6000`），因此 Kafka 消息会广播到所有服务器。每个服务器在自己的 `_userConnMap` 中查找目标用户，只有目标用户实际连接的那台服务器执行投递。

### 8.4 难点四：C++11/14 升级到 C++17

**问题**：项目原本使用 C++14 标准，但 c++_mcp 库需要 C++17 特性（如 `std::optional`、结构绑定等）。

**解决**：
- 修改 CMakeLists.txt 中的 `CMAKE_CXX_STANDARD` 从 14 改为 17
- 验证所有现有代码在 C++17 下编译通过（C++17 向后兼容 C++14）
- 确认 muduo、librdkafka、hiredis 等第三方库都支持 C++17

### 8.5 难点五：参数类型匹配

**问题**：MCP 协议要求工具参数声明类型（number、string、boolean 等），但 C++ 内部使用 `nlohmann::json` 类型。

**解决**：
- 声明阶段：使用 `with_number_param` / `with_string_param` 声明类型
- 执行阶段：通过 `params["key"].get<int>()` / `params["key"].get<string>()` 安全提取

```cpp
// 类型声明
.with_number_param("user_id", "The ID of the user", true)
.with_string_param("password", "The user's password", true)

// 类型安全提取
int userId = params["user_id"].get<int>();      // 自动类型转换和校验
string password = params["password"].get<string>();
```

---

## 9. 面试问答准备

### Q1：为什么选择 MCP 协议而不是直接写 REST API？

**答**：MCP 是 AI 行业的标准协议（由 Anthropic 提出）。如果自己写 REST API，需要：
- 自己定义 JSON 格式
- 自己写 OpenAPI 文档
- AI 客户端需要专门适配

而使用 MCP 后，任何支持 MCP 的 AI 客户端（opencode、Claude Desktop、Cursor 等）都能**零配置**调用我们的工具。MCP 定义了统一的 Tool 描述格式和 JSON-RPC 调用规范。

### Q2：MCP 登录和 TCP 客户端的登录有什么区别？

**答**：

| 维度 | MCP 登录 | TCP 客户端登录 |
|------|---------|---------------|
| 目的 | 身份验证，获取用户信息 | 建立持久连接，接收实时消息 |
| 状态 | 不修改 state，不插入 _userConnMap | 设 state="online"，插入 _userConnMap |
| 消息接收 | ❌ 不能（无 TCP 连接） | ✅ 实时推送 |
| 消息发送 | ✅ 可以（通过 sendMessageByMcp） | ✅ 可以（通过 oneChat） |
| 用途 | AI Agent 代为操作 | 真实用户实时聊天 |

### Q3：如何保证 MCP 线程与主线程的并发安全？

**答**：三层保护：
1. `_userConnMap` 用 `std::mutex _connMutex` 保护，所有读写都通过 `lock_guard` 加锁
2. 数据库连接各自从 `ConnectionPool` 获取独立连接，无共享状态
3. `KafkaManager` 内部自带 mutex，保证生产/消费线程安全
4. muduo 的 `TcpConnection::send()` 是多线程安全的

### Q4：sendMessageByMcp 如何保证消息不丢失？

**答**：三层保障：
1. 目标在线（本机）→ `TcpConnection::send()` 直接推送，TCP 保证可靠传输
2. 目标在线（其他服务器）→ Kafka 持久化消息，ACK 机制保证不丢
3. 目标离线 → MySQL 存储离线消息，用户登录时批量推送并删除

与原生客户端的 `oneChat` 使用完全相同的投递路径。

### Q5：如果目标用户切换服务器怎么办？

**答**：Kafka 广播模型天然解决了这个问题。消息发布到 `user_messages` topic 后，所有服务器都会收到消息。每台服务器独立检查自己的 `_userConnMap`，只有当前保有目标用户连接的那台服务器执行投递。用户无论切换到哪台服务器，消息都能投递到。

### Q6：c++_mcp 库是如何集成的？

**答**：采用了**源码嵌入**方式而非动态库链接。将 c++_mcp 的全部 .cpp 源文件放入项目的 `c++_mcp/` 目录，通过 CMake 的 `file(GLOB MCP_SOURCES "c++_mcp/src/*.cpp")` 编译进最终的 ChatServer 二进制文件。

**优点**：
- 编译时无外部依赖
- 方便修改和调试
- 部署简单（单二进制文件）

**代价**：
- 编译时间略有增加（+~15个源文件）
- 需要项目升级到 C++17

### Q7：MCP 工具的参数是如何传递给 handler 的？

**答**：c++_mcp 库内置了 JSON-RPC 2.0 解析器。当 AI 客户端调用工具时：
1. 客户端发送 JSON：`{"method":"tools/call","params":{"name":"chat_send_message","arguments":{"from_user_id":1,...}}}`
2. c++_mcp 解析 JSON，提取 `arguments`
3. 调用注册的 handler lambda，传入 `const json& params`（即 arguments）
4. handler 通过 `params["from_user_id"].get<int>()` 提取参数
5. handler 返回 `json` 对象，c++_mcp 序列化为 JSON-RPC 响应

### Q8：如何测试 MCP 功能？

**答**：
1. 启动服务：`./bin/ChatServer 127.0.0.1 6000 --mcp-port 8888`
2. 使用 opencode 连接本项目的 MCP Server，用自然语言测试
3. 日志中搜索 `[MCP]` 前缀查看 MCP 工具调用记录
4. 检查 MySQL `offlinemessage` 表确认离线消息存储

```sql
-- 验证离线消息是否存入
SELECT * FROM offlinemessage;
```

### Q9：未来可以如何扩展 MCP 功能？

**答**：
1. **消息撤回**：添加 `chat_recall_message` 工具
2. **群发消息**：添加 `chat_broadcast` 工具（向所有在线用户广播）
3. **消息搜索**：添加 `chat_search_messages` 工具（全文搜索历史消息）
4. **用户管理**：添加 `chat_ban_user` / `chat_kick_group` 等管理工具
5. **实时通知**：通过 SSE 向 AI 推送服务器事件（如用户上线/下线）

---

## 附录：文件清单

| 文件 | 作用 |
|------|------|
| `src/server/mcp/chat_mcp_server.h` | ChatMcpServer 单例包装器头文件（33行） |
| `src/server/mcp/chat_mcp_server.cpp` | 8个Tool注册 + 服务器启动逻辑（317行） |
| `c++_mcp/` | 第三方 MCP 协议库（15+个源文件） |
| `src/server/chatservice.cpp` | `sendMessageByMcp` 实现 + 线程安全访问器 |
| `include/server/chatservice.hpp` | `sendMessageByMcp` 方法声明 |
| `src/server/model/usermodel.cpp` | `queryByName` 实现（按用户名查询） |
| `include/server/model/usermodel.hpp` | `queryByName` 方法声明 |
| `src/server/main.cpp` | `--mcp-port` 参数解析与启动 |
| `CMakeLists.txt` | C++17 标准设置 |
| `src/server/CMakeLists.txt` | c++_mcp 源文件编译与链接 |
| `Dockerfile.server` | MCP 端口暴露 |
| `docker-compose.yml` | MCP 端口映射 (8888-8890) |
| `README.md` | MCP 工具表 + 使用示例 |
| `CLAUDE.md` | MCP 架构说明（AI 开发文档） |