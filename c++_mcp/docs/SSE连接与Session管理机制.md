# MCP Server SSE 连接与 Session 管理机制

## 概述

本文档解释 MCP Server 如何在 SSE (Server-Sent Events) 长连接架构中追踪客户端地址，以及如何将处理结果通过正确的连接返回。

## 1. SSE 连接建立流程

### 1.1 连接建立

```
客户端 ──► GET /sse ──► 服务端监听端口
```

当客户端连接到 `/sse` 端点时，核心流程在 `handle_sse()` 函数中：

```cpp
// mcp_server.cpp:587-691
void server::handle_sse(const httplib::Request& req, httplib::Response& res) {
    // 1. 生成唯一 session_id
    std::string session_id = generate_session_id();

    // 2. 创建该 session 专用的 event_dispatcher (生产者-消费者队列)
    auto session_dispatcher = std::make_shared<event_dispatcher>();

    // 3. 建立映射关系: session_id <-> event_dispatcher
    session_dispatchers_[session_id] = session_dispatcher;

    // 4. 设置 SSE 响应头
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");

    // 5. 启动心跳线程，保持连接活跃
    auto thread = std::make_unique<std::thread>([...session_id...] {
        // 定期发送 heartbeat 到客户端
        session_dispatcher->send_event(heartbeat.str());
    });

    // 6. 设置 chunked_content_provider，用于向客户端推送数据
    res.set_chunked_content_provider("text/event-stream", 
        [this, session_id, session_dispatcher](size_t, httplib::DataSink& sink) {
            // 这里会阻塞等待，直到有数据需要发送给客户端
            bool result = session_dispatcher->wait_event(&sink);
            return result;
        });
}
```

### 1.2 关键数据结构

```cpp
// session_id 与 event_dispatcher 的映射表
std::map<std::string, std::shared_ptr<event_dispatcher>> session_dispatchers_;

// event_dispatcher 内部是一个生产者-消费者模式
class event_dispatcher {
    std::mutex m_;
    std::condition_variable cv_;
    std::string message_;          // 待发送的消息
    std::atomic<bool> closed_;     // 连接是否关闭
};
```

## 2. 追踪客户端的核心机制

### 2.1 TCP 连接本身就是追踪机制

关键点：**HTTP 服务器（httplib）已经帮你维护了客户端连接**。

当你使用 `httplib::Server` 时：
- 每个 TCP 连接由操作系统分配一个 socket 文件描述符
- httplib 在内部维护了一个连接表，记录了每个 socket 的状态
- 当客户端发送 HTTP 请求时，TCP 连接已经建立，服务端知道客户端 IP:端口

### 2.2 为什么不需要显式存储客户端地址？

```
┌─────────────────────────────────────────────────────────────┐
│                      TCP 连接层                              │
│   客户端 <══════════════════════ TCP Socket ═══════════════►│
│   IP:Port                         │                    Server│
│                                    │                         │
│   连接建立时，TCP三次握手完成，    │                         │
│   双方都记录了对方的地址信息        │                         │
└─────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────┐
│                    Session 管理层                            │
│                                                             │
│   session_dispatchers_["abc123"] ──► event_dispatcher      │
│                                    │                         │
│                                    ├── message_ (待发送数据) │
│                                    ├── cv_ (条件变量)         │
│                                    └── closed_ (状态)         │
│                                        │                     │
│                                        │                     │
│                                        ▼                     │
│                              通过 DataSink 写入 socket      │
└─────────────────────────────────────────────────────────────┘
```

### 2.3 数据流向

```
HTTP POST /message                    SSE 响应
┌─────────┐    ┌──────────────┐    ┌─────────────────────┐
│ 客户端   │───►│ handle_mcp_post│───►│ 放入线程池处理      │
└─────────┘    └──────────────┘    └─────────────────────┘
                                              │
                                              ▼
                                       ┌──────────────┐
                                       │ process_request│
                                       └──────────────┘
                                              │
                                              ▼
┌─────────┐    ┌──────────────────────┐    ┌────────────┐
│ 客户端   │◄───│ DataSink.write()     │◄───│ session_   │
│         │    │ (通过 socket 发送)    │    │ dispatcher │
└─────────┘    └──────────────────────┘    └────────────┘
     ▲                                        ▲
     │                                        │
     │           SSE 长连接                   │
     └────────────────────────────────────────┘
```

## 3. 完整请求处理流程

### 3.1 POST /message 处理流程 (mcp_server.cpp:828)

```cpp
void server::handle_mcp_post(const httplib::Request& req, httplib::Response& res) {
    // 1. 从请求头获取 session_id
    std::string session_id = req.get_header_value("Mcp-Session-Id");

    // 2. 找到对应的 event_dispatcher
    std::lock_guard<std::mutex> lock(mutex_);
    auto dispatcher = session_dispatchers_[session_id];

    // 3. 解析 JSON-RPC 请求
    request mcp_req = parse_jsonrpc_message(body);

    // 4. 放入线程池处理
    thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
        // 5. 处理请求
        json response_json = process_request(mcp_req, session_id);

        // 6. 通过 SSE 推送结果给客户端
        std::stringstream ss;
        ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
        dispatcher->send_event(ss.str());
    });
}
```

### 3.2 process_request 处理 (mcp_server.cpp:1078)

```cpp
json server::process_request(const request& req, const std::string& session_id) {
    // 调用注册的 handler，传入 session_id 供回调使用
    handler(req.params, session_id);
    // ...
}
```

### 3.3 event_dispatcher 生产者-消费者

```cpp
bool event_dispatcher::send_event(const std::string& message) {
    std::lock_guard<std::mutex> lk(m_);
    message_ = message;
    cid_.store(id_.fetch_add(1, std::memory_order_relaxed));
    cv_.notify_one();  // 通知消费者有新消息
    return true;
}

bool event_dispatcher::wait_event(httplib::DataSink* sink, ...) {
    std::unique_lock<std::mutex> lk(m_);
    cv_.wait_for(lk, timeout, [...] { return cid_changed; });

    // 写入到 SSE 流
    sink->write(message_copy.data(), message_copy.size());
    return true;
}
```

## 4. 总结

### 4.1 如何追踪客户端？

1. **TCP Socket 追踪**：每个连接是一个 socket，操作系统记录了 IP:Port
2. **Session 映射**：服务端用 `session_id` 作为 key，映射到对应的 `event_dispatcher`
3. **请求关联**：HTTP 请求头中的 `Mcp-Session-Id` 把请求和 session 关联起来

### 4.2 如何找到正确的客户端？

1. 客户端 POST 请求时携带 `Mcp-Session-Id` 头
2. 服务端用这个 session_id 查 `session_dispatchers_` map
3. 找到对应的 `event_dispatcher`，通过其内部的 DataSink 写入数据
4. DataSink 绑定到 TCP socket，直接发送给客户端

### 4.3 为什么不需要存储客户端地址？

因为：
- SSE 是**持久化的 TCP 连接**
- 数据通过同一个 socket 发送，无需知道地址（socket 已经知道）
- `session_id` 只是用来区分不同的客户端连接

## 5. 类比理解

可以把整个架构想象成：

```
                        ┌─────────────────┐
                        │   服务端         │
                        │                 │
                        │  ┌───────────┐  │
                        │  │ Session表  │  │
                        │  │            │  │
    ┌─────┐             │  │ id1 ───────┼──┼──► socket1 ──► 客户端A
    │客户端│───► socket0│  │            │  │
    │  A   │             │  │ id2 ───────┼──┼──► socket2 ──► 客户端B
    └─────┘             │  │            │  │
                        │  │ id3 ───────┼──┼──► socket3 ──► 客户端C
    ┌─────┐             │  └───────────┘  │
    │客户端│───► socket3│                  │
    │  C   │             │                 │
    └─────┘             └─────────────────┘
```

每个 session_id 对应一个 socket，数据通过 socket 自动发送到正确的客户端。