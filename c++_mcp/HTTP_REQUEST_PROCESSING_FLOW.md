# C++ MCP SDK HTTP 请求处理流程详解

## 概述

本文档详细讲解 MCP 服务器的 HTTP 请求处理流程，包括：
1. 服务器启动和监听
2. 请求路由分配
3. 线程池处理
4. SSE 连接建立和管理
5. 事件分发机制

---

## 1. 整体架构流程图

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                        MCP 服务器完整处理流程                           │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
  │   客户端   │ ──► │  httplib   │ ──► │ 路由分发   │ ──► │ 线程池    │
  │            │      │  HTTP 服务器│      │           │      │           │
  │ • SSE 连接 │      │  listen()  │      │ /message   │      │ 动态扩展  │
  │ • HTTP POST│      │            │      │ /sse       │      │           │
  │ • HTTP GET │      │            │      │ /mcp       │      │ min→max   │
  └─────────────┘      └─────────────┘      └─────────────┘      └─────┬─────┘
                                                                  │
                                                                  ▼
  ┌─────────────┐      ┌─────────────┐      ┌─────────────┐      ┌─────────────┐
  │  SSE 连接  │ ◄─── │ event_    │ ◄─── │ 处理函数   │ ◄─── │ 任务取出   │
  │  发送响应 │      │ dispatcher│      │ 调用      │      │ 执行      │
  │           │      │           │      │           │      │           │
  │ wait_event│      │ send_event│      │ tool_handler│     │           │
  └───────────┘      └───────────┘      └─────────────┘      └─────────────┘
```

---

## 2. 服务器启动流程 (start)

### 2.1 start() 函数概览

```cpp
// mcp_server.cpp 第120-214行
bool server::start(bool blocking) {
    // 1. 设置 CORS 处理
    // 2. 注册路由处理函数
    // 3. 启动维护线程（非阻塞模式）
    // 4. 启动 HTTP 服务器监听
}
```

### 2.2 启动步骤详解

```
start(true/false)
      │
      ▼
┌─────────────────────┐
│ 1. 设置 CORS 头      │  处理跨域请求
│ OPTIONS handler     │
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 2. 注册路由函数     │
│ • POST /message    │  JSON-RPC 端点（2024-11-05）
│ • GET /sse         │  SSE 端点（2024-11-05）
│ • POST /mcp        │  Streamable HTTP（2025-03-26）
│ • GET /mcp         │  SSE 流（2025-03-26）
│ • DELETE /mcp      │  删除会话
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 3. 启动维护线程    │  仅非阻塞模式
│ maintenance_thread│  检查不活跃会话
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 4. 启动 HTTP 监听 │
│                    │
│ 阻塞模式:         │
│   http_server_->  │
│   listen()       │
│                    │
│ 非阻塞模式:      │
│   新线程中调用    │
│   listen()       │
└──────────┬──────────┘
           │
           ▼
    [HTTP 服务器运行中]
    等待客户端连接
```

### 2.3 httplib 的 listen()

**关键点**：httplib 内部有自己的线程处理机制！

```cpp
// httplib 内部实现（简化）
http_server_->listen(host, port) {
    // 1. 创建 server socket
    // 2. 循环 accept() 获取新连接
    // 3. 为每个连接创建新线程 或 使用内部线程池
    // 4. 在线程中调用注册的 handler
}
```

**httplib 处理流程**：

```
客户端连接
     │
     ▼
┌─────────────────────────┐
│  accept() 获取新连接    │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  创建处理线程           │  httplib 内部线程
│  （或线程池）          │
└───────────┬─────────────┘
            │
            ▼
┌─────────────────────────┐
│  调用注册的 handler   │  handle_sse / handle_jsonrpc
│                       │  handle_mcp_post 等
└───────────┬─────────────┘
            │
            ▼
      [请求处理完成]
```

---

## 3. 请求路由详解

### 3.1 路由注册代码

```cpp
// mcp_server.cpp 第127-162行
// 1. JSON-RPC 端点（传统方式）
http_server_->Post(msg_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
    this->handle_jsonrpc(req, res);
});

// 2. SSE 端点（传统方式）
http_server_->Get(sse_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
    this->handle_sse(req, res);
});

// 3. Streamable HTTP（新版）
http_server_->Post(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
    this->handle_mcp_post(req, res);
});

http_server_->Get(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
    this->handle_mcp_get(req, res);
});

http_server_->Delete(mcp_endpoint_.c_str(), [this](const httplib::Request& req, httplib::Response& res) {
    this->handle_mcp_delete(req, res);
});
```

### 3.2 路由对应关系

| 端点路径 | HTTP 方法 | 用途 | 传输协议 |
|----------|----------|------|--------|---------|
| `/message` | POST | 发送 JSON-RPC 请求 | HTTP+SSE (2024-11-05) |
| `/sse` | GET | 建立 SSE 连接 | HTTP+SSE (2024-11-05) |
| `/mcp` | POST | 发送请求/初始化 | Streamable HTTP (2025-03-26) |
| `/mcp` | GET | 建立 SSE 流 | Streamable HTTP (2025-03-26) |
| `/mcp` | DELETE | 关闭会话 | Streamable HTTP (2025-03-26) |

---

## 4. 线程池处理

### 4.1 任务提交到线程池

当请求到达时，处理函数会将任务提交到您升级的动态线程池：

```cpp
// mcp_server.cpp 第886-916行
// handle_jsonrpc 函数中的处理

// 1. 通知类型（无 ID）��理：异步
if (mcp_req.is_notification()) {
    thread_pool_.enqueue([this, mcp_req, session_id]() {
        process_request(mcp_req, session_id);  // 在线程池中处理
    });
    res.status = 202;
    return;
}

// 2. 有 ID 的请求：异步处理，结果通过 SSE 返回
thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
    // 处理请求
    json response_json = process_request(mcp_req, session_id);
    
    // 通过 SSE 发送响应
    std::stringstream ss;
    ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
    dispatcher->send_event(ss.str());
});

res.status = 202;
```

### 4.2 线程池处理流程

```
thread_pool_.enqueue(task)
         │
         ▼
┌─────────────────────┐
│ 1. 创建 Task       │  包装成 std::function
└──────────┬──────────┘
           │
           ▼
┌─────────────────────┐
│ 2. 检查空闲线程   │ idle_threads_ == 0?
└──────────┬──────────┘
           │
    ┌──────┴──────┐
    │是          │否
    ▼            ▼
┌─────────────┐ ┌─────────────────────┐
│ 3. 自动扩展 │ │ 4. 任务加入队列   │
│ spawn_thread│ │ tasks_.emplace()   │
└─────────────┘ └──────────┬──────────┘
                          │
                          ▼
                   ┌─────────────────────┐
                   │ 5. notify_one()  │  唤醒一个空闲线程
                   └────────┬────────┘
                           │
                           ▼
                    [工作线程取出任务]
                           │
                           ▼
                   ┌─────────────────────┐
                   │ 6. 执行任务      │  调用 tool_handler
                   └────────┬────────┘
                           │
                           ▼
                    [返回 future 或通过 SSE 返回]
```

### 4.3 动态线程池的优势

1. **自动扩展**：任务多时自动创建新线程
2. **防止饥饿**：不会因为线程不够导致任务堆积
3. **资源回收**：空闲太久自动退出多余线程

---

## 5. 事件分发机制 (event_dispatcher)

### 5.1 event_dispatcher 设计

这是**生产者-消费者模式**的实现，用于 SSE 推送：

```cpp
// mcp_server.h 第45-167行
class event_dispatcher {
private:
    mutable std::mutex m_;                    // 互斥锁
    std::condition_variable cv_;             // 条件变量
    std::atomic<int> id_;                 // 消息版本号
    std::atomic<int> cid_;                 // 已消费版本号
    std::string message_;                   // 消息内容
    std::atomic<bool> closed_{false};      // 关闭标志
    std::chrono::steady_clock::time_point last_activity_; // 最后活动时间
};
```

### 5.2 生产者：发送事件

```cpp
// 发送端（SSE 客户端）
bool send_event(const std::string& message) {
    std::lock_guard<std::mutex> lk(m_);
    
    message_ = message;           // 写入消息
    cid_.store(id_.fetch_add(1)); // 增加版本号
    cv_.notify_one();          // 唤醒等待的消费者
    return true;
}
```

### 5.3 消费者：等待事件

```cpp
// 接收端（SSE 连接）
bool wait_event(httplib::DataSink* sink, timeout) {
    std::unique_lock<std::mutex> lk(m_);
    
    // 等待新��息��超时
    bool result = cv_.wait_for(lk, timeout, [&] {
        return cid_ != id_ || closed_;
    });
    
    if (!result) return false;  // 超时
    
    // 写入 HTTP 响应
    sink->write(message_.data(), message_.size());
    return true;
}
```

### 5.4 生产者-消费者流程

```
┌─────────────────────────────────────────────────────┐
│              event_dispatcher 工作流程                 │
└─────────────────────────────────────────────────────┘

  【生产者端】                                    【消费者端】
  ┌──────────────┐                           ┌──────────────┐
  │ tool_handler│                           │ SSE 连接    │
  │   处理请求   │                           │ wait_event()│
  └──────┬─────┘                           └──────┬─────┘
         │                                          │
         ▼                                          ▼ (阻塞等待)
  ┌──────────────┐                           ┌──────────────┐
  │ process_    │                           │ wait_for()  │
  │ request()  │                           │  - 等待新消息│
  └──────┬─────┘                           │  - 超时退出│
         │                              └──────┬─────┘
         │                                     │
         ▼                                     ▼
  ┌──────────────┐                      [写入 HTTP 流]
  │ send_event()│
  │            │
  │ message_= │ ◄──────────────────────────────┘
  │   data    │           notify_one()
  │            │
  │ cid_.++  │                              
  └──────┬─────┘
         │
         ▼
    [任务完成，HTTP 响应已发送]
```

---

## 6. SSE 连接建立流程

### 6.1 SSE 连接何时建立？

**SSE 连接在客户端发起请求时建立！**

两种建立方式：

#### 方式 1：传统 SSE 端点（/sse）

```
客户端                                服务器
  │                                    │
  │──── GET /sse HTTP/1.1 ────────────►│
  │                                    │
  │                                    │── handle_sse()
  │                                    │   1. 创建 session_id
  │                                    │   2. 创建 event_dispatcher
  │                                    │   3. 启动 SSE 线程
  │                                    │   4. 设置 chunked provider
  │◄── 200 OK (chunked) ────────────────│
  │     Content-Type: text/event-stream│
  │                                    │
  │──── [SSE 连接保持] ───────────────►│
  │     wait_event() 阻塞等待          │
  │                                    │
  │    [后续 POST /message 发送请求]   │
```

#### 方式 2：Streamable HTTP（/mcp）

```
客户端                                服务器
  │                                    │
  │──── POST /mcp (initialize) ───────►│ handle_mcp_post()
  │   {method: initialize, ...}          │   1. 创建 session
  │                                    │   2. 创建 dispatcher
  │◄── 200 OK + Mcp-Session-Id ────────│   3. 返回响应
  │                                    │
  │──── GET /mcp (带 session) ─────────►│ handle_mcp_get()
  │   Mcp-Session-Id: xxx               │   设置 chunked provider
  │◄── 200 OK (chunked) ──────────────│
  │     Content-Type: text/event-stream │
  │                                    │
  │──── [SSE 连接保持] ───────────────►│
```

### 6.2 handle_sse() 详细流程

```cpp
// mcp_server.cpp 第684-801行
void server::handle_sse(const httplib::Request& req, httplib::Response& res) {
    // 1. 检查会话数量限制
    if (session_dispatchers_.size() >= max_sessions_) {
        res.status = 503;
        return;
    }
    
    // 2. 生成 session_id
    std::string session_id = generate_session_id();
    
    // 3. 创建 event_dispatcher（生产者-消费者）
    auto session_dispatcher = std::make_shared<event_dispatcher>();
    
    // 4. 保存到会话表
    session_dispatchers_[session_id] = session_dispatcher;
    
    // 5. 启动 SSE 线程（发送心跳、保持连接）
    auto thread = std::make_unique<std::thread>([...]() {
        // 发送初始 endpoint
        session_dispatcher->send_event("event: endpoint\r\ndata: " + session_uri + "\r\n\r\n");
        
        // 循环发送心跳（每5秒）
        while (running_ && !dispatcher->is_closed()) {
            std::this_thread::sleep_for(5s);
            session_dispatcher->send_event("event: heartbeat\r\ndata: " + ...);
        }
        
        // 连接关闭时清理
        close_session(session_id);
    });
    
    // 6. 设置 chunked 响应（关键！）
    res.set_chunked_content_provider("text/event-stream", 
        [session_id, session_dispatcher](size_t offset, httplib::DataSink& sink) {
            // 消费者在这里等待事件
            return session_dispatcher->wait_event(&sink);
        });
}
```

### 6.3 SSE 连接建立详细时序图

```
时间线
  │
  │ 客户端                      服务器                      线程池
  │  │                           │                           │
  │ │                           │                           │
  │ │─────── GET /sse ─────────>│                           │
  │ │                           │                           │
  │ │                    handle_sse()                      │
  │ │                           │── 创建 session_id         │
  │ │                           │── 创建 dispatcher      │
  │ │                           │── 保存到 map           │
  │ │                           │── spawn SSE thread     │
  │ │                           │   (发送心跳)         │
  │ │                           │                           │
  │ │<──── 200 OK (chunked)────│                           │
  │ │     + session_uri       │                           │
  │ │                           │                           │
  │ │================== SSE 长连接建立 ==================│
  │ │                           │                           │
  │ │                           │    wait_event() 阻塞     │
  │ │                           │    (等待消息)         │
  │ │                           │                           │
  │ │                           │                           │
  │ │                           │    [空闲等待]          │
  │ │                           │                           │
  │ │                           │                           │
  │ │──── POST /message ───────>│                           │
  │ │     session_id           │                           │
  │ │     + JSON-RPC 请求     │                           │
  │ │                           │                           │
  │ │                    handle_jsonrpc()               │
  │ │                           │                           │
  │ │                           │── thread_pool_.enqueue()  │
  │ │                           │   [task]              │
  │ │                           │                           │
  │ │<──── 202 Accepted ──────│                           │
  │ │                           │                           │
  │ │                           │── 线程池取出任务       │
  │ │                           │   process_request()    │
  │ │                           │   → tool_handler()     │
  │ │                           │                           │
  │ │                           │── dispatcher->        │
  │ │                           │      send_event()       │
  │ │                           │                           │
  │ │                    [SSE 连接触发]                    │
  │ │                    wait_event() 返回              │
  �� │                           │                           │
  │ │◄─── event: message ──────│                           │
  │ │     data: {...}          │                           │
  │ │                           │                           │
  │ │================== 继续等待 ===================│
  │ │                           │                           │
```

---

## 7. 完整请求处理流程

### 7.1 端到端流程图

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│              完整的一次请求处理流程                      │
└─────────────────────────────────────────────────────────────────────────────────┘

客户端              httplib              MCP服务器            线程池           工具处理
  │                   │                   │                   │               │
  │                   │                   │                   │               │
  │─ GET /sse ─────► │                   │                   │               │
  │                 │───────────────────>│ handle_sse()      │               │
  │                 │                   │ ─ 创建 session   │               │
  │                 │                   │ ─ 创建 dispatcher│               │
  │                 │                   │ ─ 启动 SSE 线程 │               │
  │                 │                   │ ─ set_chunked    │               │
  │◄── 200 OK ──────│                   │                   │               │
  │     (SSE 连接) │                   │                   │               │
  │                 │                   │   [等待消息]     │               │
  │                 │                   │   wait_event()   │               │
  │                 │                   │                   │               │
  │─ POST /message─►│                   │                   │
  │     session_id │───────────────────>│ handle_jsonrpc() │               │
  │     JSON-RPC │                   │                   │               │
  │                 │                   │ ─ thread_pool   │               │
  │                 │                   │    .enqueue()  │               │
  │◄── 202 ────────│                   │   [task]       │               │
  │                 │                   │       │         │               │
  │                 │                   │       ▼         │               │
  │                 │                   │  [取出任务]    │───►    │
  │                 │                   │       │         │       │
  │                 │                   │       ▼         │       │
  │                 │                   │  tool_handler   │◄───────│
  │                 │                   │       │         │       │
  │                 │                   │       ▼         │       │
  │                 │                   │  返回结果      │       │
  │                 │                   │       │         │       │
  │                 │                   │       ▼         │       │
  │                 │                   │  send_event()  │       │
  │                 │                   │   [消息就绪]  │       │
  │                 │                   │       │         │       │
  │◄─ event: ──────│                   │   wait_event() │       │
  │    message    │                   │   [返回消息]   │       │
  │                 │                   │                   │               │
  │                 │                   │                   │               │
```

### 7.2 关键点总结

1. **httplib 监听**：httplib 的 `listen()` 在后台循环接受连接
2. **路由匹配**：根据 URL 路径匹配到对应的 handler
3. **SSE 连接建立**：
   - 传统：客户端请求 `/sse` 端点时建立
   - 新版：客户端 POST initialize 后，GET `/mcp` 时建立
4. **线程池处理**：请求处理在您升级的动态线程池中
5. **事件分发**：通过 `event_dispatcher` 实现生产者和消费者的同步
6. **响应发送**：处理完成后，通过 SSE 连接推送回客户端

---

## 8. 您的理解基本正确，补充说明

您说的流程基本正确，这里补充几点：

### 8.1 您的描述 vs 实际

| 您说的 | 实际情况 |
|--------|---------|
| start() 启动后台线程 | httplib 内部有线程，非阻塞模式下 MCP 服务器也启动独立线程 |
| httplib 监听循环 | httplib 的 `listen()` 内部循环 accept() |
| httplib 线程池 | httplib 为每个连接创建线程（或内部线程池） |
| 路由处理函数 | 直接调用或在线程池中调用 |
| 项目自己的线程池 | `thread_pool_.enqueue()` 提交处理任务 |
| 处理函数调用 | 在项目线程池的工作线程中执行 |
| 事件分发对象 | `event_dispatcher`，生产者-消费者模式 |
| SSE 发送 | SSE 连接通过 `wait_event()` 等待，`send_event()` 发送 |

### 8.2 SSE 连接建立时机

- **方式 1**：客户端主动请求 `/sse` 端点 → 服务器创建 SSE 连接
- **方式 2**：
  1. 客户端 POST `/mcp` (initialize) → 创建 session
  2. 客户端 GET `/mcp` → 服务器建立 SSE 流

### 8.3 两层线程处理

```
┌─────────────────────────────────────────┐
│         两层线程处理                   │
└─────────────────────────────────────────┘

  第 1 层：httplib 内部线程
  ┌─────────────────────────────────────┐
  │ • accept() 获取连接                  │
  │ • 为每个连接创建处理线程            │
  │ • 调用注册的 handler                │
  └─────────────────────────────────────┘
           │
           ▼
  第 2 层：MCP 线程池（您的升级）
  ┌─────────────────────────────────────┐
  │ • 处理 JSON-RPC 请求                 │
  │ • 调用 tool_handler                  │
  │ • 动态扩展/收缩                    │
  └─────────────────────────────────────┘
```

---

## 9. 代码位置索引

| 功能 | 文件位置 |
|------|---------|
| 服务器启动 | `src/mcp_server.cpp:120-214` |
| 路由注册 | `src/mcp_server.cpp:127-162` |
| SSE 处理 | `src/mcp_server.cpp:684-801` |
| JSON-RPC 处理 | `src/mcp_server.cpp:803-916` |
| Streamable HTTP | `src/mcp_server.cpp:937-1185` |
| 线程池提交 | `src/mcp_server.cpp:888-916` |
| event_dispatcher | `include/mcp_server.h:45-167` |
| 线程池实现 | `include/mcp_thread_pool.h` |

---

*文档生成时间：2026-04-28*