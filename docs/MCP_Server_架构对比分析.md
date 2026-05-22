# MCP Server 架构对比分析：内嵌式 vs 中间件式

> 本文档详细对比分析了两个聊天服务器项目中 MCP Server 的两种实现方式，涵盖架构设计、代码实现、数据流和工程权衡。

---

## 目录

1. [项目背景概述](#1-项目背景概述)
2. [架构一：内嵌式 MCP Server（集群版）](#2-架构一内嵌式-mcp-server集群版)
3. [架构二：中间件式 MCP Server（Windows版）](#3-架构二中间件式-mcp-serverwindows版)
4. [c++_mcp 框架详解](#4-c_mcp-框架详解)
5. [两种架构的深度对比](#5-两种架构的深度对比)
6. [代码级对比](#6-代码级对比)
7. [优缺点总结与选型建议](#7-优缺点总结与选型建议)

---

## 1. 项目背景概述

### 1.1 集群版项目 (`E:\song-集群聊天服务器`)

- **底层框架**: muduo (Reactor 模式) + Protobuf 序列化
- **数据库**: MySQL 8.0 主从复制 + 读写分离
- **缓存**: Redis + Sentinel 哨兵模式
- **跨服通信**: Kafka 消息队列广播
- **负载均衡**: Nginx TCP Stream
- **MCP 版本**: 2025-03-26 (Streamable HTTP) + 兼容 2024-11-05 (HTTP+SSE)

### 1.2 Windows 版项目 (`E:\ChatServer_win`)

- **底层框架**: 自研 Boost.Asio TCP Server + JSON 协议
- **数据库**: MySQL 单机
- **跨服通信**: 无（单机部署）
- **MCP 版本**: 2024-11-05 (HTTP+SSE)

---

## 2. 架构一：内嵌式 MCP Server（集群版）

### 2.1 总体架构

MCP Server 作为**主进程中的一个独立线程**运行，直接复用 `ChatService` 单例提供的所有业务功能、数据模型和系统状态。

```
┌──────────────────────── ChatServer 进程 ────────────────────────┐
│                                                                  │
│  ┌──────────────┐    ┌─────────────────────────────────────┐    │
│  │ muduo 主线程 │    │       MCP HTTP 线程池 (1~4 工人)      │    │
│  │ (6000端口)  │    │         (8888 端口)                  │    │
│  │              │    │                                     │    │
│  │ TCP Client──▶│    │  AI Client ──▶ HTTP /mcp            │    │
│  │ Protobuf     │    │    (Claude/OpenCode)                │    │
│  └──────┬───────┘    └──────────────┬──────────────────────┘    │
│         │                           │                           │
│         └───────────┬───────────────┘                           │
│                     ▼                                           │
│         ┌─────────────────────────┐                            │
│         │    ChatService 单例      │                            │
│         │  (所有业务逻辑)          │                            │
│         │  _userConnMap (在线用户)  │                            │
│         │  UserModel / GroupModel   │                            │
│         │  FriendModel / OfflineMsg │                            │
│         │  sendMessageByMcp()      │                            │
│         │  getConnectionCount()    │                            │
│         │  getOnlineUserIds()      │                            │
│         └──────────┬──────────────┘                            │
│                    │                                            │
│         ┌──────────┼──────────┬──────────┐                     │
│         ▼          ▼          ▼          ▼                     │
│    ┌────────┐ ┌────────┐ ┌────────┐ ┌───────┐                │
│    │ MySQL  │ │ Redis  │ │ Kafka  │ │ 缓存层 │                │
│    │ (主从) │ │+Sentinel│ │消息队列│ │       │                │
│    └────────┘ └────────┘ └────────┘ └───────┘                │
│                                                                  │
└──────────────────────────────────────────────────────────────────┘
```

### 2.2 关键实现文件

| 文件 | 职责 |
|------|------|
| `src/server/main.cpp` | 入口，解析 `--mcp-port` 参数，启动 MCP Server |
| `src/server/mcp/chat_mcp_server.h` | ChatMcpServer 头文件（单例模式） |
| `src/server/mcp/chat_mcp_server.cpp` | 注册 8 个 MCP 工具 + Handler 实现 |
| `src/server/chatservice.cpp` | `sendMessageByMcp()` 方法，给 MCP 调用的消息发送 |
| `c++_mcp/src/mcp_server.cpp` | MCP 协议核心引擎（1555 行） |
| `c++_mcp/src/mcp_tool.cpp` | tool_builder 链式 API |

### 2.3 启动流程

```
main() 启动
    │
    ├── 解析 CLI 参数
    │      ./ChatServer 127.0.0.1 6000 --mcp-port 8888
    │
    ├── ProtoMsgHandlerMap::registerHandler()  注册 8 个 Protobuf 消息处理器
    │
    ├── if (g_mcpPort > 0):
    │      ChatMcpServer::instance()->start(g_mcpPort)   ← MCP Server 在此启动
    │           │
    │           ├── 创建 mcp::server 实例 (host="0.0.0.0", port=8888)
    │           ├── set_server_info("ChatClusterServer", "1.0.0")
    │           ├── set_instructions("This MCP server provides...")
    │           ├── registerTools()  ← 注册 8 个工具 + Handler
    │           └── server_->start(false)  ← 非阻塞，拉起 HTTP 线程池
    │
    └── muduo EventLoop::loop()  ← 主线程进入 Reactor 循环
```

### 2.4 工具注册详解

`ChatMcpServer::registerTools()` 在 `chat_mcp_server.cpp:85-316` 中注册了以下 8 个工具：

#### 工具 1: `chat_server_stats` — 服务器统计

```cpp
server_->register_tool(
    mcp::tool_builder("chat_server_stats")
        .with_description("Get cluster chat server statistics including connection count and online user count")
        .build(),
    [svc](const json&, const string&) -> json {
        size_t connCount = svc->getConnectionCount();          // 直接读 _userConnMap.size()
        auto onlineIds = svc->getOnlineUserIds();               // 直接读 _userConnMap 的 key
        return {
            {"connections", connCount},
            {"onlineUsers", onlineIds.size()},
            {"serverInfo", "Cluster Chat Server v1.0.0"}
        };
    }
);
```

**关键特征**: 直接访问 `ChatService` 的内部状态 `_userConnMap`，获取服务器的实时连接数和在线用户列表。这是一个**普通 TCP 客户端完全不可能做到的事情**——它属于运维管理级别的操作。

#### 工具 2: `chat_list_online_users` — 在线用户列表

```cpp
[svc](const json&, const string&) -> json {
    auto ids = svc->getOnlineUserIds();
    json result = json::array();
    auto& userModel = svc->getUserModel();
    for (int id : ids) {
        User user = userModel.query(id);    // 查数据库/缓存获取用户名
        if (user.getId() != -1) {
            result.push_back({{"id", user.getId()}, {"name", user.getName()}});
        } else {
            result.push_back({{"id", id}, {"name", "unknown"}});
        }
    }
    return {{"onlineUsers", result}, {"count", ids.size()}};
}
```

**关键特征**: 同时使用了**内存状态**（`_userConnMap` 获取在线 ID 列表）和**数据模型**（`UserModel::query()` 查用户名），体现了内嵌式架构对全部层级的访问能力。

#### 工具 3: `chat_get_user_info` — 用户信息查询

```cpp
.with_number_param("user_id", "The ID of the user to query", true)
[svc](const json& params, const string&) -> json {
    int userId = params["user_id"].get<int>();
    User user = svc->getUserModel().query(userId);
    if (user.getId() == -1) return {{"error", "User not found"}};
    return {{"user", userToJson(user)}, {"isOnline", user.getState() == "online"}};
}
```

**关键特征**: 直接调用 `UserModel::query()`（底层走 `CacheManager` 先查 Redis 缓存，miss 后查 MySQL 从库），返回结果还包括数据库中的 `state` 字段判断在线状态。

#### 工具 4: `chat_get_user_friends` — 好友列表

```cpp
[svc](const json& params, const string&) -> json {
    int userId = params["user_id"].get<int>();
    User user = svc->getUserModel().query(userId);
    vector<User> friends = svc->getFriendModel().query(userId);
    // 构建 JSON 返回
    return {{"userId", userId}, {"friends", friendList}, {"count", friends.size()}};
}
```

**关键特征**: 通过 `FriendModel::query()` 查询好友关系（底层通过 `CacheManager` 缓存），返回结构化数据。

#### 工具 5: `chat_get_group_info` — 群组信息

```cpp
.with_number_param("group_id", "The ID of the group to query", true)
[svc](const json& params, const string&) -> json {
    int groupId = params["group_id"].get<int>();
    Group group = svc->getGroupModel().queryGroup(groupId);
    // 返回 groupId, groupName, description, members (含角色), memberCount
}
```

**关键特征**: 直接访问 `GroupModel`，获取群组成员列表及其角色（creator/normal）。

#### 工具 6: `chat_list_user_groups` — 用户群组列表

```cpp
[svc](const json& params, const string&) -> json {
    int userId = params["user_id"].get<int>();
    vector<Group> groups = svc->getGroupModel().queryGroups(userId);
    // 返回群组列表（id, name, desc, memberCount）
}
```

#### 工具 7: `chat_user_login` — 用户认证登录

```cpp
.with_number_param("user_id", "The numeric ID of the user to login as", true)
.with_string_param("password", "The user's password", true)
[svc](const json& params, const string&) -> json {
    User user = svc->getUserModel().query(userId, true);  // forceMaster=true 读主库
    if (user.getId() == -1) return {{"success", false}, {"error", "User not found"}};
    if (user.getPwd() != password) return {{"success", false}, {"error", "Invalid password"}};
    if (user.getState() == "online") return {{"success", false}, {"error", "User is already logged in"}};
    // 返回 user + friends + groups
    return {{"success", true}, {"userId", user.getId()}, {"userName", user.getName()},
            {"friends", friendsJson}, {"groups", groupsJson}};
}
```

**关键特征**: 登录验证走 `UserModel::query(userId, true)` —— `forceMaster=true` 强制读 MySQL 主库，避免主从复制延迟导致的密码验证失败。返回完整的用户信息，包括好友列表和群组列表。

#### 工具 8: `chat_send_message` — 发送私聊消息

```cpp
.with_number_param("from_user_id", "The sender's numeric user ID", true)
.with_number_param("to_user_id", "The recipient's numeric user ID", true)
.with_string_param("message", "The message content to send", true)
[svc](const json& params, const string&) -> json {
    // 1. 参数校验（非空、非自己）
    // 2. 验证发送者和接收者存在
    // 3. 调用 svc->sendMessageByMcp(fromId, toId, message)
    // 4. 返回发送结果（deliveryMethod: "direct" or "offline_stored"）
}
```

**关键特征**: MCP 发送消息使用的是专门的方法 **`sendMessageByMcp()`**（定义在 `src/server/chatservice.cpp:539-572`），而非直接调用 `oneChat()`。这是因为 `oneChat()` 的函数签名依赖 muduo 的 `TcpConnectionPtr` 参数，而 MCP 调用场景下不存在这个连接对象。

### 2.5 `sendMessageByMcp()` 方法解析

```cpp
bool ChatService::sendMessageByMcp(int fromId, int toId, const string& messageContent) {
    // 1. 构造 Protobuf OneChatMessage
    chat::OneChatMessage chatMsg;
    chatMsg.mutable_base()->set_msgid(chat::ONE_CHAT_MSG);
    chatMsg.mutable_base()->set_fromid(fromId);
    chatMsg.mutable_base()->set_toid(toId);
    chatMsg.mutable_base()->set_time(muduo::Timestamp::now().microSecondsSinceEpoch());
    chatMsg.set_message(messageContent);
    string serializedMsg = chatMsg.SerializeAsString();

    // 2. 在线用户且在本服务器 → 直接发送
    {
        lock_guard<mutex> lock(_connMutex);
        auto it = _userConnMap.find(toId);
        if (it != _userConnMap.end()) {
            it->second->send(serializedMsg);       // 通过 muduo TCP 连接直接推送
            return true;
        }
    }

    // 3. 在线但不在本服务器 → 通过 Kafka 广播到所有服务器
    User user = _userModel.query(toId);
    if (user.getState() == "online") {
        if (_kafkaManager) {
            _kafkaManager->sendMessage("user_messages", serializedMsg);
        }
        return true;
    }

    // 4. 离线 → 存入 MySQL 离线消息表
    _offlineMsgModel.insert(toId, serializedMsg);
    return true;
}
```

**核心逻辑**: 该方法复用了一对一聊天的**完整投递路径**：
1. **内存查找** `_userConnMap`（在线+本服→直发）
2. **Kafka 广播**（在线+异服→消息队列中转）
3. **MySQL 离线消息表**（离线→持久化存储）

这意味着 MCP 发出去的消息和普通客户端发出的消息走完全相同的投递链路。

### 2.6 线程安全设计

内嵌式架构中，MCP Handler 和 muduo 网络 I/O 线程**共享同一个 ChatService 单例**，因此必须处理线程安全问题：

```
muduo I/O 线程                     MCP HTTP 工作线程
     │                                    │
     │  oneChat()                          │  chat_send_message handler
     │    lock(_connMutex)                 │    sendMessageByMcp()
     │    _userConnMap.find(toid)         │      lock(_connMutex)
     │    ...                              │      _userConnMap.find(toId)
     │                                    │
     ▼                                    ▼
     两者通过 _connMutex 互斥，保证 _userConnMap 的并发安全
```

`_connMutex` 在 `ChatService` 中定义，所有读写 `_userConnMap` 的操作都必须持有该锁。

---

## 3. 架构二：中间件式 MCP Server（Windows 版）

### 3.1 总体架构

MCP Server 是一个**完全独立**的进程 (`McpChatServer.exe`)，它同时扮演两个角色：
1. **对 AI 客户端**: 它是一个 MCP Server（HTTP+SSE，监听 8888 端口）
2. **对聊天服务器**: 它是一个**普通的 TCP 客户端**（连接 6000 端口，使用 JSON 协议）

```
┌──────────────┐     HTTP/SSE     ┌──────────────────────┐     TCP/JSON     ┌──────────────────┐
│  OpenCode    │ ◄──────────────► │  McpChatServer.exe   │ ◄──────────────► │  ChatServer.exe  │
│  (AI Agent)  │   JSON-RPC 2.0   │                      │   \n 分隔符      │  (6000端口)      │
│              │                  │  ┌────────────────┐  │                  │                  │
│              │                  │  │ mcp::server    │  │                  │  ┌─────────────┐ │
│              │                  │  │ (MCP 服务器)   │  │                  │  │ ChatService │ │
│  "帮我登录"  │                  │  │ 监听 :8888     │  │                  │  │ JSON 路由   │ │
│      │       │                  │  └───────┬────────┘  │                  │  │             │ │
│      ▼       │                  │          │           │                  │  └──────┬──────┘ │
│  调用 login  │───tools/call──►│          ▼           │                  │         │        │
│  tool        │                  │  ┌────────────────┐  │                  │         ▼        │
│              │◄───JSON-RPC result─│  │ ChatClient    │  │                  │   UserModel     │
│              │                  │  │ Wrapper        │───{"msgid":1,...}─►│   FriendModel   │
│              │                  │  │ (TCP 客户端)   │  │                  │   ...           │
│              │                  │  │ 连接 :6000     │  │                  │                 │
│              │                  │  └────────────────┘  │                  └──────────────────┘
│              │                  │                      │
└──────────────┘                  └──────────────────────┘
```

### 3.2 关键实现文件

| 文件 | 职责 |
|------|------|
| `src/McpChatServer.cpp` | **唯一的文件**，包含 ChatClientWrapper 类 + main 函数（共 221 行） |
| `c++_mcp/...` | 同上，复用的 c++_mcp 框架（与集群版**不同的版本**，旧版 API） |
| `src/ChatServer.cpp` | 聊天服务器主逻辑（完全不知道 MCP 的存在） |
| `src/chatservice.cpp` | 业务逻辑（JSON 协议处理） |
| `include/public.hpp` | 消息 ID 枚举（LOGIN_MSG=1, ONE_CHAT_MSG=6...） |

### 3.3 启动流程

```
步骤 1: 启动 ChatServer.exe (监听 6000)
步骤 2: 启动 McpChatServer.exe (监听 8888，连接 6000)
         │
         ├── ChatClientWrapper client("127.0.0.1", "6000")  ← 建立 TCP 长连接
         │    ├── boost::asio::connect(socket_, endpoints)  ← 同步连接
         │    └── reader_thread_ 启动，持续读取服务器响应
         │
         ├── mcp::server server("localhost", 8888)  ← 创建 MCP HTTP 服务器
         │
         ├── 注册 3 个 MCP Tools + Handler
         │    ├── login:        MCP params → TCP JSON {"msgid":1,...}  → 等待 ACK
         │    ├── send_message:  MCP params → TCP JSON {"msgid":6,...}  → 不等 ACK
         │    └── get_offline_messages: 从内缓存读取
         │
         └── server.start(true)  ← 阻塞，开始监听 HTTP SSE 请求
```

### 3.4 核心：ChatClientWrapper 类

这是整个中间件的**核心组件**，封装了 TCP 客户端的所有逻辑：

#### 构造函数 —— 建立 TCP 长连接 + 启动读取线程

```cpp
ChatClientWrapper(const std::string& host, const std::string& port)
    : io_context_(), socket_(io_context_)
{
    // 1. DNS 解析
    tcp::resolver resolver(io_context_);
    auto endpoints = resolver.resolve(host, port);

    // 2. 同步 TCP 连接
    boost::asio::connect(socket_, endpoints);

    // 3. 启动独立线程持续异步读取服务器消息
    reader_thread_ = std::thread([this]() {
        try {
            doRead();           // 注册异步读取回调
            io_context_.run();  // 进入 Boost.Asio 事件循环
        } catch (std::exception& e) {
            std::cerr << "Reader thread exception: " << e.what() << "\n";
        }
    });
}
```

#### sendRequest() —— 发送 TCP 消息 + 可选阻塞等待 ACK

```cpp
json sendRequest(const json& req, bool waitForAck, int ackMsgId = -1) {
    std::string msg = req.dump() + "\n";  // JSON + \n 分隔符

    std::unique_lock<std::mutex> lock(mutex_);

    // 如果需要等 ACK，设置"期望的消息 ID"
    if (waitForAck) {
        expected_ack_msgid_ = ackMsgId;  // 例如 LOGIN_MSG_ACK = 2
        ack_received_ = false;
    }

    // 同步发送（boost::asio::write 阻塞直到数据发送完毕）
    boost::asio::write(socket_, boost::asio::buffer(msg));

    // 阻塞等待 ACK（通过条件变量 cv_）
    if (waitForAck) {
        cv_.wait(lock, [this]() { return ack_received_; });
        return last_ack_json_;           // 返回服务器响应
    }

    return json::object();  // 不需要等 ACK，立即返回空对象
}
```

**关键设计**: 这个方法是**同步阻塞的**。由于 MCP Tool Handler 的返回值必须同步生成，而底层的 TCP 读取是异步的（在独立线程中），这里使用了 `std::condition_variable` 将异步的 TCP 响应"缝合"成了同步的函数返回。

#### doRead() —— 异步读取 + 消息分类

```cpp
void doRead() {
    boost::asio::async_read_until(socket_, boost::asio::dynamic_buffer(read_msg_), "\n",
        [this](boost::system::error_code ec, std::size_t length) {
            if (!ec) {
                std::string msg = read_msg_.substr(0, length - 1);
                read_msg_.erase(0, length);

                try {
                    json js = json::parse(msg);
                    int msgid = js["msgid"].get<int>();

                    std::lock_guard<std::mutex> lock(mutex_);

                    if (msgid == expected_ack_msgid_) {
                        // ★ 这是我们在等待的 ACK 响应 ★
                        last_ack_json_ = js;
                        ack_received_ = true;
                        expected_ack_msgid_ = -1;
                        cv_.notify_one();    // 唤醒 sendRequest() 中阻塞的线程
                    }
                    else if (msgid == ONE_CHAT_MSG || msgid == GROUP_CHAT_MSG) {
                        // ★ 这是服务器主动推送的聊天消息 ★
                        offline_messages_.push_back(js);  // 缓存起来
                    }
                } catch (...) { /* 忽略解析错误 */ }

                doRead();  // 继续读取下一条
            }
        });
}
```

**关键设计**:
- 通过 `expected_ack_msgid_` 判断收到的消息是"我们等待的 ACK"还是"服务器主动推送的消息"
- ACK 消息通过 `cv_.notify_one()` 唤醒 `sendRequest()` 中阻塞的线程
- 服务器推送的消息（私聊/群聊）缓存到 `offline_messages_` 中，后续可以用 `get_offline_messages` 工具获取

### 3.5 三个 MCP 工具及其 Handler

#### 工具 1: `login` — 登录

```cpp
mcp::tool login_tool = mcp::tool_builder("login")
    .with_description("Login to ChatServer")
    .with_number_param("id", "User ID")
    .with_string_param("password", "Password")
    .build();

server.register_tool(login_tool, [&client](const mcp::json& params, const string&) -> mcp::json {
    // 1. 将 MCP 参数打包成聊天服务器的 JSON 协议格式
    json req;
    req["msgid"] = LOGIN_MSG;          // msgid=1
    req["id"] = params["id"].get<int>();
    req["password"] = params["password"].get<string>();

    // 2. 通过 TCP 客户端发送，等待 msgid=2 的 ACK
    json ack = client.sendRequest(req, true, LOGIN_MSG_ACK);  // 阻塞等待

    // 3. 将服务器返回的 JSON 包装成 MCP 格式返回
    return mcp::json::array({
        {{"type", "text"}, {"text", ack.dump()}}  // 直接透传服务器响应
    });
});
```

**数据流**:
```
AI 调用 login(id=8, password="123")
    ↓
McpChatServer handler 构造 {"msgid":1, "id":8, "password":"123"}
    ↓
ChatClientWrapper::sendRequest() → TCP write → ChatServer:6000
    ↓
ChatServer 验证 → 返回 {"msgid":2, "errno":0, "id":8, "name":"zhangsan"}
    ↓
ChatClientWrapper::doRead() 识别 msgid==2 → cv_.notify_one()
    ↓
sendRequest() 解锁 → 返回 ack
    ↓
包装成 MCP result.content = [{"type":"text", "text":"{\"errno\":0,..."} }]
    ↓
返回给 AI 客户端
```

#### 工具 2: `send_message` — 发送私聊消息

```cpp
server.register_tool(send_msg_tool, [&client](const mcp::json& params, const string&) -> mcp::json {
    json req;
    req["msgid"] = ONE_CHAT_MSG;     // msgid=6

    // 支持 toid（用户ID）或 toname（用户名）两种指定方式
    if (params.contains("toid") && !params["toid"].is_null()) {
        req["toid"] = params["toid"].get<int>();
    } else if (params.contains("toname") && !params["toname"].is_null()) {
        req["toname"] = params["toname"].get<string>();
    }

    req["msg"] = params["msg"].get<string>();

    // ★ 发送消息不等待 ACK（ChatServer 不会向发送者回复 ACK）★
    client.sendRequest(req, false);

    return {{"type", "text"}, {"text", "Message sent"}};
});
```

**关键特征**: `sendRequest(req, false)` —— `waitForAck = false`，因为 ChatServer 的 `ONE_CHAT_MSG` 不会向发送者返回 ACK（只有 `LOGIN_MSG` 和 `REG_MSG` 会返回 ACK）。这反映了中间件的处理逻辑依赖于对**下游服务通信协议的充分了解**。

#### 工具 3: `get_offline_messages` — 获取离线/推送消息

```cpp
server.register_tool(get_msgs_tool, [&client](const mcp::json&, const string&) -> mcp::json {
    std::vector<json> msgs = client.getOfflineMessages();  // 从内缓存读取
    json result = msgs;
    return {{"type", "text"}, {"text", result.dump()}};
});
```

**关键特征**: 从 `offline_messages_` 内缓存中读取服务器主动推送过来的消息。由于 McpChatServer 是以一个"**已登录用户**"的身份连接 ChatServer 的，所以聊天服务器推送过来的私聊/群聊消息会被 `doRead()` 自动缓存在 `offline_messages_` 中。

### 3.6 ChatServer 端的 JSON 协议

Windows 版聊天服务器使用 **JSON + '\n' 分隔符** 作为 TCP 通信协议：

| 消息类型 | msgid | 发送字段 | 响应字段 |
|---------|-------|---------|---------|
| 登录请求 | 1 (LOGIN_MSG) | `id`, `password` | — |
| 登录响应 | 2 (LOGIN_MSG_ACK) | — | `errno`, `id`, `name` |
| 登出 | 3 (LOGINOUT_MSG) | `id` | — |
| 注册 | 4 (REG_MSG) | `name`, `password` | — |
| 注册响应 | 5 (REG_MSG_ACK) | — | `errno`, `id` |
| 私聊消息 | 6 (ONE_CHAT_MSG) | `toid`/`toname`, `msg` | — |
| 群聊消息 | 10 (GROUP_CHAT_MSG) | `groupid`, `msg` | — |

**ChatServer 的消息路由逻辑** (`src/ChatServer.cpp:50-59`):

```cpp
void ChatServer::onMessage(const TcpConnectionPtr& conn, const string& msg, Timestamp ts) {
    json js = json::parse(msg);
    auto handler = ChatService::instance()->getHandler(js["msgid"].get<int>());
    handler(conn, js, ts);   // 根据 msgid 分发到对应的 handler
}
```

---

## 4. c++_mcp 框架详解

两个项目都使用了 `c++_mcp` 库，但**版本和使用方式有所不同**。

### 4.1 框架核心类

| 类 | 文件 | 职责 |
|----|------|------|
| `mcp::server` | `mcp_server.h/cpp` (1555行) | MCP 协议引擎：HTTP 服务器 + JSON-RPC 路由 + SSE 事件推送 |
| `mcp::tool` | `mcp_tool.h` | 工具定义：name, description, inputSchema |
| `mcp::tool_builder` | `mcp_tool.h/cpp` | 链式 API 构建工具定义 |
| `mcp::request` | `mcp_message.h` | JSON-RPC 2.0 请求封装 |
| `mcp::response` | `mcp_message.h` | JSON-RPC 2.0 响应封装 |
| `event_dispatcher` | `mcp_server.h` | SSE 事件分发器：条件变量 + 消息队列 |
| `thread_pool` | `mcp_thread_pool.h` | 工作线程池 |

### 4.2 mcp::server 的两套 HTTP API

```
Streamable HTTP (2025-03-26):              Legacy HTTP+SSE (2024-11-05):
    POST /mcp   ← JSON-RPC请求                GET  /sse     ← SSE长连接
    GET  /mcp   ← SSE流 (服务器推送)          POST /message ← JSON-RPC请求
    DELETE /mcp ← 会话关闭
```

### 4.3 请求处理流程

```
HTTP POST /mcp (body: JSON-RPC)
    │
    ▼
handle_mcp_post()  [mcp_server.cpp:937]
    │
    ├── 解析 JSON body
    ├── 获取 Mcp-Session-Id 头
    ├── 若是 initialize → 创建 session，返回 server_info + capabilities
    ├── 若是 tools/call → 调用 process_request()
    │       │
    │       ▼
    │   process_request()  [mcp_server.cpp:1187]
    │       │
    │       ├── req.method == "initialize" → handle_initialize()
    │       ├── req.method == "ping" → response::create_success()
    │       ├── 查 method_handlers_ 表 → 找到 tools/call handler
    │       │       │
    │       │       ├── 从 tools_ map 中按 name 找到工具
    │       │       ├── 调用 tool_handler(params, session_id)
    │       │       └── 返回 result with MCP content format
    │       └── 未找到 → response::create_error(method_not_found)
    │
    └── 返回 JSON response body
```

**工具注册时的自动 handler 注册** (`mcp_server.cpp:557-613`):

```cpp
void server::register_tool(const tool& tool, tool_handler handler) {
    tools_[tool.name] = make_pair(tool, handler);

    // ★ 自动注册 tools/list 方法 ★
    method_handlers_["tools/list"] = [this](...) -> json {
        json tools_json = json::array();
        for (const auto& [name, tool_pair] : tools_) {
            tools_json.push_back(tool_pair.first.to_json());
        }
        return json{{"tools", tools_json}};
    };

    // ★ 自动注册 tools/call 方法 ★
    method_handlers_["tools/call"] = [this](const json& params, const string& session_id) -> json {
        string tool_name = params["name"];
        auto it = tools_.find(tool_name);
        // ...
        tool_result["content"] = it->second.second(tool_args, session_id);
        return tool_result;
    };
}
```

这意味着开发者只需调用 `register_tool(name, handler)`，框架就自动注册好了 `tools/list` 和 `tools/call` 两个 MCP 标准方法。

### 4.4 MCP 初始化握手

```
Client                               Server
  │                                    │
  │ POST /mcp                          │
  │ { "method": "initialize",          │
  │   "params": {                      │
  │     "protocolVersion": "2025-03-26",│
  │     "clientInfo": {...}            │
  │   }                                │
  │ }                                  │
  ├───────────────────────────────────►│  handle_mcp_post()
  │                                    │  → 创建 session_id
  │◄───────────────────────────────────┤  → 返回 Mcp-Session-Id 头
  │ { "result": {                      │  → 返回 serverInfo
  │     "protocolVersion": "...",      │  → 返回 capabilities
  │     "serverInfo": {...},           │  → 返回 instructions
  │     "capabilities": {...}          │
  │ } }                                │
  │                                    │
  │ POST /mcp                          │
  │ Mcp-Session-Id: xxx                │
  │ { "method": "notifications/initialized" } │
  ├───────────────────────────────────►│  → 设置 session 为已初始化
  │                                    │
  │ [后续请求...]                       │
```

### 4.5 两个项目使用的版本差异

| 特性 | 集群版使用的 c++_mcp | Windows 版使用的 c++_mcp |
|------|---------------------|-------------------------|
| 构造方式 | `mcp::server(config_struct)` 配置结构体 | `mcp::server("localhost", 8888)` 字符串参数 |
| MCP 协议版本 | 2025-03-26 (Streamable HTTP on `/mcp`) | 2024-11-05 (HTTP+SSE on `/sse`) |
| 非阻塞启动 | `server.start(false)` → 独立线程 | `server.start(true)` → 阻塞当前线程 |
| 线程池 | `config.thread_pool_min_size/max_size` | 固定 |
| 会话管理 | session timeout / max_sessions | 默认实现 |

---

## 5. 两种架构的深度对比

### 5.1 整体架构对比

```
┌─ 内嵌式 (Embedded) ───────────────────────────────────────────────────┐
│                                                                         │
│   MCP Server  =  主机体上的一个器官                                      │
│   (同一个进程地址空间，共享内存、文件描述符、锁)                           │
│                                                                         │
│   ChatServer 进程                                                        │
│   ┌──────────────────────────────────────────────────────────────────┐ │
│   │  muduo主线程 │  MCP线程池 │  Kafka消费者线程 │  维护线程           │ │
│   │  (TCP 6000)  │  (HTTP8888) │ (跨服消息)       │ (session清理)     │ │
│   └──────┬───────┴─────┬───────┴──────┬───────────┴─────┬────────────┘ │
│          └─────────────┴──────────────┴─────────────────┘               │
│                                 │                                        │
│                    ChatService (共享单例)                                 │
│                    UserModel, FriendModel, ...                          │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘

┌─ 中间件式 (Proxy) ─────────────────────────────────────────────────────┐
│                                                                         │
│   MCP Server  =  主机体外部的一个翻译官                                  │
│   (独立进程，通过 TCP 协议和主机体通信)                                   │
│                                                                         │
│   McpChatServer.exe (独立进程)                ChatServer.exe (独立进程)   │
│   ┌──────────────────────────────────┐     ┌──────────────────────────┐ │
│   │  mcp::server  (HTTP SSE :8888)  │     │  TcpServer (TCP :6000)   │ │
│   │     │                           │     │     │                    │ │
│   │     │ tools/call handler        │     │     │ JSON 协议分发      │ │
│   │     │  ├── login handler        │     │     │ ChatService        │ │
│   │     │  │    │                  │     │     │ UserModel           │ │
│   │     │  │    ▼                  │     │     │ FriendModel         │ │
│   │     │  │  ChatClientWrapper    │     │     └────────────────────┘ │
│   │     │  │   └── TCP socket ─────┼─────┼──► :6000                  │ │
│   │     │  │                      │     │                            │ │
│   │     │  ├── send_message       │     │                            │ │
│   │     │  └── get_offline_msgs   │     │                            │ │
│   └──────────────────────────────────┘     └──────────────────────────┘ │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

### 5.2 多维度对比表

| 对比维度 | 内嵌式（集群版） | 中间件式（Windows 版） |
|---------|----------------|----------------------|
| **进程模型** | MCP 在 ChatServer 进程内 | MCP 独立进程，与 ChatServer 分离 |
| **通信方式** | C++ 函数直接调用 | TCP Socket + JSON 协议 |
| **耦合度** | **强耦合**：MCP 代码导入所有 ChatServer 头文件 | **零耦合**：ChatServer 完全不知道 MCP 的存在 |
| **部署方式** | `./ChatServer 127.0.0.1 6000 --mcp-port 8888` | 先启动 `ChatServer.exe`，再启动 `McpChatServer.exe` |
| **性能延迟** | **≈ 0ms**：纯内存函数调用，无网络开销 | **≈ 1-2ms**：TCP 本地回环 + JSON 序列化/反序列化 |
| **工具数量/能力** | 8 个工具，涵盖管理+业务 | 3 个工具，仅涵盖基础业务 |
| **服务器内部状态** | ✅ 可访问（连接数、在线用户列表等） | ❌ 无法访问（只能做普通客户端能做的事） |
| **发送消息机制** | 走完整投递链路（内存→Kafka→离线） | 通过 TCP JSON 协议发请求 |
| **认证方式** | 数据库查询验证 | 委托给 ChatServer 验证 |
| **稳定性** | MCP 崩溃 = 整个服务器崩溃 | MCP 崩溃 = 仅 AI 无法接入 |
| **需要了解的内容** | 全套 C++ 源码、内存模型、线程安全 | 仅需 TCP JSON 协议文档 |
| **聊天服务器是否需改动** | **需要**：新增 `sendMessageByMcp()` 等 | **不需要**：MCP 只是另一个 TCP 客户端 |
| **序列化格式** | Protobuf（服务器内部） | JSON（MCPServer ↔ ChatServer 之间） |

### 5.3 数据流对比

#### 内嵌式：AI 调用 `chat_user_login`

```
AI Client              MCP HTTP Handler           ChatService / Model
   │                         │                          │
   │ tools/call              │                          │
   │ name:"chat_user_login"  │                          │
   ├────────────────────────►│                          │
   │                         │ params["user_id"]=8      │
   │                         │ params["password"]="123" │
   │                         │                          │
   │                         │── User user = svc->      │
   │                         │    getUserModel()        │
   │                         │    .query(8, true) ─────►│  forceMaster=true
   │                         │                          │  查 MySQL 主库
   │                         │◄───── User(id=8,       ──┤
   │                         │        name="zhangsan")  │
   │                         │                          │
   │                         │── vector<User> friends = │
   │                         │    svc->getFriendModel()  │
   │                         │    .query(8) ───────────►│  查好友列表
   │                         │◄──── {bob(2), cathy(3)}─┤
   │                         │                          │
   │◄─── JSON result ────────┤                          │
   │     {success:true,       │                          │
   │      userName:"zhangsan",│                          │
   │      friends:[...]}      │                          │
```

**特点**: AI 请求 → C++ 函数调用 → C++ 函数返回。全程**零网络跳转**，**零序列化开销**（除 MCP JSON-RPC 本身）。

#### 中间件式：AI 调用 `login`

```
AI Client          McpChatServer                ChatServer
   │                    │                          │
   │ tools/call         │                          │
   │ name:"login"       │                          │
   ├───────────────────►│                          │
   │                    │ ChatClientWrapper        │
   │                    │ .sendRequest(req_json,    │
   │                    │   waitForAck=true,       │
   │                    │   ackMsgId=2)            │
   │                    │                          │
   │                    │ 组装: {"msgid":1,         │
   │                    │        "id":8,           │
   │                    │        "password":"123"}  │
   │                    │                          │
   │                    │── boost::asio::write ───►│
   │                    │     TCP Socket           │
   │                    │                          │  JSON 解析
   │                    │                          │  msgid=1 → login handler
   │                    │                          │  UserModel::query(8)
   │                    │                          │  密码验证
   │                    │◄─ "{\"errno\":0,     ────┤
   │                    │    \"name\":\"zhangsan\", │
   │                    │    \"msgid\":2}"          │
   │                    │                          │
   │                    │ doRead() 识别 msgid==2    │
   │                    │ cv_.notify_one() 唤醒    │
   │                    │ sendRequest() 解锁返回   │
   │                    │                          │
   │◄── JSON-RPC result ┤                          │
   │   content: {ack.dump()}                       │
```

**特点**: AI 请求 → JSON 序列化 → TCP 发送 → TCP 读取 → JSON 反序列化 → 服务器处理 → JSON 响应序列化 → TCP 发送 → TCP 读取 → JSON 反序列化 → AI 响应。共经历 **2 次网络传输** + **4 次 JSON 序列化/反序列化**。

---

## 6. 代码级对比

### 6.1 架构对比：启动代码

#### 内嵌式 (`main.cpp` + `chat_mcp_server.cpp`)

```cpp
// main.cpp — MCP Server 在主进程中启动
if (g_mcpPort > 0) {
    ChatMcpServer::instance()->start(g_mcpPort);
}
// 然后 muduo 主循环继续
EventLoop loop;
server.start();
loop.loop();
```

```cpp
// chat_mcp_server.cpp — 直接复用 ChatService 单例
bool ChatMcpServer::start(uint16_t port) {
    server_ = make_unique<mcp::server>(config);
    server_->set_server_info("ChatClusterServer", "1.0.0");
    registerTools();                                    // 注册 8 个工具
    server_->start(false);                             // 非阻塞，拉起 HTTP 线程
    return true;
}

void ChatMcpServer::registerTools() {
    auto* svc = ChatService::instance();               // ★ 直接获取单例
    server_->register_tool("chat_server_stats", ...,
        [svc](const json&, const string&) -> json {
            return {
                {"connections", svc->getConnectionCount()},  // ★ 直接调用 C++ 方法
                {"onlineUsers", svc->getOnlineUserIds().size()}
            };
        }
    );
    // ... 8 个工具全部直接操作 svc->xxxModel()
}
```

#### 中间件式 (`McpChatServer.cpp`)

```cpp
int main() {
    // 1. 首先建立到 ChatServer 的 TCP 连接
    ChatClientWrapper client("127.0.0.1", "6000");

    // 2. 创建 MCP Server
    mcp::server server("localhost", 8888);
    server.set_server_info("McpChatServer", "1.0.0");

    // 3. 注册工具 — 每个 handler 都是通过 TCP 客户端发送消息
    server.register_tool(login_tool,
        [&client](const mcp::json& params, ...) -> mcp::json {
            json req;                                      // ★ 手动拼 JSON 协议
            req["msgid"] = LOGIN_MSG;
            req["id"] = params["id"];
            req["password"] = params["password"];
            json ack = client.sendRequest(req, true, LOGIN_MSG_ACK);  // ★ TCP 发送+阻塞等待
            return {{"type", "text"}, {"text", ack.dump()}};
        }
    );

    // 4. 阻塞启动
    server.start(true);
}
```

### 6.2 Handler 写法对比

#### 同一个功能：登录

```cpp
// ★ 内嵌式 ★
[svc](const json& params, const string&) -> json {
    User user = svc->getUserModel().query(params["user_id"].get<int>(), true);
    if (user.getPwd() != params["password"]) return {{"success", false}};
    // 返回结构化 JSON：userId, userName, friends[], groups[]
    return {{"success", true}, {"userId", user.getId()}, ...};
}
```

```cpp
// ★ 中间件式 ★
[&client](const mcp::json& params, const string&) -> mcp::json {
    json req;
    req["msgid"] = 1;                         // ← 硬编码的消息 ID
    req["id"] = params["id"].get<int>();
    req["password"] = params["password"].get<string>();
    json ack = client.sendRequest(req, true, 2);  // ← 阻塞等 ACK
    return {{"type", "text"}, {"text", ack.dump()}};  // ← 直接透传原始 JSON
}
```

#### 同一个功能：发送消息

```cpp
// ★ 内嵌式 ★
[svc](const json& params, const string&) -> json {
    bool ok = svc->sendMessageByMcp(fromId, toId, message);
    // 返回 deliveryMethod: "direct" or "offline_stored"
    return {{"success", ok}, {"deliveryMethod", ...}};
}
```

```cpp
// ★ 中间件式 ★
[&client](const mcp::json& params, const string&) -> mcp::json {
    json req;
    req["msgid"] = 6;               // ← ONE_CHAT_MSG
    req["toid"] = params["toid"];
    req["msg"] = params["msg"];
    client.sendRequest(req, false);  // ← 不等 ACK
    return {{"type", "text"}, {"text", "Message sent"}};
}
```

### 6.3 c++_mcp 库版本差异

#### 集群版使用的 API（新版本）

```cpp
// 构造: 使用配置结构体
mcp::server::configuration config;
config.host = "0.0.0.0";
config.port = port;
config.thread_pool_min_size = 1;
config.thread_pool_max_size = 4;
config.max_sessions = 10;
config.session_timeout = chrono::seconds(60);

server_ = make_unique<mcp::server>(config);

// 启动: 非阻塞模式
server_->start(false);   // 在独立线程中启动 HTTP Server

// MCP 端点: /mcp (2025-03-26 Streamable HTTP)
```

#### Windows 版使用的 API（旧版本）

```cpp
// 构造: 使用字符串参数
mcp::server server("localhost", 8888);

// 启动: 阻塞模式
server.start(true);      // 阻塞当前线程，直接调用 listen()

// MCP 端点: /sse + /message (2024-11-05 HTTP+SSE)
```

---

## 7. 优缺点总结与选型建议

### 7.1 内嵌式（集群版）

**优点**:
1. **性能极致**: 工具调用是纯内存的函数调用，无序列化/网络开销
2. **能力无上限**: 可以读取 ChatServer 的**任意内部状态**（连接数、在线列表、Kafka 状态等），这是运维管理工具的天然优势
3. **深度整合**: 与服务器共享数据库连接池、Redis 缓存、Kafka 生产者，资源利用率高
4. **数据一致性**: 发送消息走完整的投递链路（内存→Kafka→离线表），行为与普通客户端完全一致
5. **返回信息丰富**: 登录时直接查数据库返回好友列表、群组列表，体验好
6. **工具覆盖全面**: 8 个工具覆盖了全部业务操作

**缺点**:
1. **强耦合**: 修改 MCP Server 需要重新编译整个 ChatServer
2. **稳定性风险**: MCP Server 的异常（JSON 解析错误、内存越界）可能导致整个聊天服务崩溃
3. **开发门槛高**: 需要深度理解 ChatServer 的源码、线程模型、锁机制
4. **部署绑定**: MCP Server 随 ChatServer 一起部署，无法独立扩缩容
5. **需要修改聊天服务器**: 新增了 `sendMessageByMcp()` 等专为 MCP 设计的代码

### 7.2 中间件式（Windows 版）

**优点**:
1. **零耦合**: ChatServer **完全不需要修改任何代码**，MCP Server 对 ChatServer 来说就是个普通 TCP 客户端
2. **故障隔离**: MCP Server 崩溃只会影响 AI 助手的接入，不影响真实用户
3. **独立部署**: 可以单独部署、升级、重启 MCP Server
4. **开发简单**: 只需要了解 ChatServer 的 TCP JSON 协议即可开发
5. **语言无关**: MCP Server 理论上可以用任何语言重写，只要会发 TCP JSON 就行
6. **天然适配微服务**: 可以作为多个微服务的统一 AI 接入网关

**缺点**:
1. **性能损耗**: 每次工具调用都有 TCP 往返 + JSON 序列化/反序列化开销
2. **能力受限**: 只能使用 ChatServer 暴露给普通客户端的 API，无法获取服务器内部状态（如连接数、在线列表）
3. **同步阻塞等待**: 对需要 ACK 的请求（如 login），`sendRequest()` 内部使用条件变量阻塞线程等待 TCP 响应
4. **协议依赖**: 工具 Handler 中硬编码了 `msgid` 字段值，如果 ChatServer 协议变动必须同步修改
5. **功能有限**: 只能做 3 个基础操作（登录、发消息、收离线消息）
6. **无跨服感知**: 不知道 Kafka、Redis 等后端基础设施的存在

### 7.3 选型决策指南

```
是否需要访问服务器内部状态？
    │
    ├── 是 ──→ 内嵌式（如运维监控、管理员工具）
    │
    └── 否
        │
        ├── 聊天服务器是否已有完善的客户端 API？
        │     ├── 是 ──→ 中间件式（ChatServer 不需要改动）
        │     └── 否 ──→ 需要先完善 API，再决定架构
        │
        ├── 是否需要故障隔离？
        │     ├── 是 ──→ 中间件式
        │     └── 否 ──→ 都可以
        │
        └── 是否需要语言无关性？
              ├── 是 ──→ 中间件式
              └── 否 ──→ 内嵌式（但必须是 C++）
```

---

## 8. 架构演进建议

两个项目目前的架构都有改进空间：

### 对集群版（内嵌式）的建议

1. **加强错误隔离**: 在每个 MCP tool handler 中使用 try-catch 包裹，防止异常导致整个进程崩溃
2. **MCP 专用 API 层**: 将 `sendMessageByMcp()` 这类方法抽象成一个独立的接口层，便于维护
3. **Tool 配置化**: 将工具定义从代码中抽离为配置文件（如 JSON Schema），支持热加载

### 对 Windows 版（中间件式）的建议

1. **连接池管理**: ChatClientWrapper 当前是单连接，如果同时多个 AI 调用并发高，需要连接池
2. **增加管理工具**: 如果 ChatServer 新增 `get_server_stats`、`list_online_users` 等 API，中间件就可以暴露更多工具
3. **协议版本管理**: 将 msgid 映射表集中管理，减少硬编码
4. **异步化 Handler**: 用 `std::future` /协程等方式优化 `sendRequest()` 的同步阻塞等待

---

*文档生成时间：2026-05-05*
*基于代码版本：集群版 (git main) 和 Windows版 (ChatServer_win)*