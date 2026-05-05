# C++ MCP SDK HTTP/SSE 方式完整工作流程

## 概述

本文档详细讲解基于 HTTP + SSE 通信方式的 MCP Server 全流程，包括：Server 端启动（httplib 监听、路由注册）、SSE 长连接建立与管理、event_dispatcher 生产者-消费者机制、线程池动态调度、请求处理与响应推送、Client 端连接与调用、完整通信时序流程。

---

## 1. 整体架构

HTTP/SSE 模式下，Server 监听一个 HTTP 端口，多个 Client 可以同时连接。每个 Client 有两步：
- **第一步**：GET /sse 建立 SSE（Server-Sent Events）长连接
- **第二步**：POST /message 发送 JSON-RPC 请求

请求在线程池中异步处理，处理完毕后通过 SSE 连接将结果推回客户端。

```
Client A -- GET  /sse  -----> SSE long connection -----> Server
Client A -- POST /message ---> JSON-RPC request -------> Server --> Thread Pool --> process_request()
Client A <-- event: message --- SSE response <----------- Server <-- dispatcher.send_event()

Client B -- GET  /sse  -----> SSE long connection -----> Server
Client B -- POST /message ---> JSON-RPC request -------> Server --> Thread Pool --> process_request()
Client B <-- event: message --- SSE response <----------- Server <-- dispatcher.send_event()
```

关键理解：
- 每个 Client 有自己的 session_id 和 event_dispatcher
- 请求处理和响应返回是异步的
- 多个客户端可以同时连接，互不干扰

---

## 2. Server 端：用户怎么写代码

用户代码分为 5 步：创建 Server 实例 -> 注册 Tools -> 注册 Prompts -> 注册 Resources -> 启动 HTTP Server。

```cpp
// 以 examples/server_example.cpp 为例

int main() {
    // === 第1步：创建 Server 实例 ===
    mcp::server::configuration srv_conf;
    srv_conf.host = "localhost";
    srv_conf.port = 8888;

    mcp::server server(srv_conf);
    server.set_server_info("ExampleServer", "1.0.0");
    server.set_capabilities({
        {"tools", mcp::json::object()}
    });

    // === 第2步：注册 Tools ===
    mcp::tool time_tool = mcp::tool_builder("get_time")
        .with_description("Get current time")
        .build();
    server.register_tool(time_tool, get_time_handler);

    mcp::tool echo_tool = mcp::tool_builder("echo")
        .with_description("Echo input")
        .with_string_param("text", "Text to echo")
        .build();
    server.register_tool(echo_tool, echo_handler);

    // === 第3步：注册 Prompts ===
    mcp::prompt hello_prompt = mcp::prompt_builder("hello_prompt")
        .with_description("Generate a greeting")
        .with_argument("name", "Name to greet", true)
        .build();
    server.register_prompt(hello_prompt, [](const json& args, ...) -> json {
        // prompt handler logic
        return mcp::json::array({message});
    });

    // === 第4步：注册 Resources ===
    auto file_resource = std::make_shared<mcp::file_resource>("./knowledge.md");
    server.register_resource("file://./knowledge.md", file_resource);

    // === 第5步：启动 HTTP Server！ ===
    server.start(true);    // 阻塞监听，httplib 启动
}
```

注意：这里调用的是 `server.start(true)` 而不是 `server.start_stdio()`。

---

## 3. Server 端：start() 怎么工作？

start() 做四件事：设置 CORS、注册 HTTP 路由、启动维护线程、调用 httplib 开始监听。

```cpp
// src/mcp_server.cpp 第120-214行
bool server::start(bool blocking) {
    if (running_) return true;

    // === 第1步：设置 CORS 处理 ===
    http_server_->Options(".*", [](const httplib::Request& req, httplib::Response& res) {
        res.set_header("Access-Control-Allow-Origin", "*");
        res.set_header("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS");
        res.status = 204;
    });

    // === 第2步：注册5个路由处理函数 ===
    // 传统 JSON-RPC 端点：POST /message -> handle_jsonrpc()
    http_server_->Post("/message", [this](auto& req, auto& res) {
        this->handle_jsonrpc(req, res);
    });

    // 传统 SSE 端点：GET /sse -> handle_sse()
    http_server_->Get("/sse", [this](auto& req, auto& res) {
        this->handle_sse(req, res);
    });

    // Streamable HTTP 端点：POST/GET/DELETE /mcp
    http_server_->Post("/mcp", ...);
    http_server_->Get("/mcp", ...);
    http_server_->Delete("/mcp", ...);

    // === 第3步：启动维护线程（非阻塞模式） ===
    // 每10秒检查不活跃会话

    // === 第4步：启动 HTTP 监听 ===
    running_ = true;
    http_server_->listen(host_.c_str(), port_);
    return true;
}
```

httplib 的 listen() 内部会：创建 server socket -> 循环 accept() 接收连接 -> 为每个连接创建处理线程 -> 调用注册的 handler 函数。

路由表：
| HTTP 方法 | 端点 | 处理函数 | 用途 |
|-----------|------|----------|------|
| POST | /message | handle_jsonrpc | 发送 JSON-RPC 请求 |
| GET | /sse | handle_sse | 建立 SSE 长连接 |
| POST | /mcp | handle_mcp_post | Streamable HTTP 请求 |
| GET | /mcp | handle_mcp_get | Streamable HTTP SSE |
| DELETE | /mcp | handle_mcp_delete | 关闭会话 |

---

## 4. SSE 长连接是怎么建立的？

### 4.1 核心代码：handle_sse()

当 Client 发送 GET /sse 请求时，Server 调用 handle_sse()。它创建 session、创建 event_dispatcher、启动心跳线程、设置 chunked 响应。

```cpp
// src/mcp_server.cpp 第684-801行
void server::handle_sse(const httplib::Request& req, httplib::Response& res) {
    // 1. 检查会话数量限制
    if (session_dispatchers_.size() >= max_sessions_) {
        res.status = 503;
        return;
    }

    // 2. 生成 session_id
    std::string session_id = generate_session_id();
    std::string session_uri = "/message?session_id=" + session_id;

    // 3. 设置 SSE 响应头
    res.set_header("Content-Type", "text/event-stream");
    res.set_header("Cache-Control", "no-cache");
    res.set_header("Connection", "keep-alive");

    // 4. 创建 event_dispatcher（关键！）
    auto session_dispatcher = std::make_shared<event_dispatcher>();

    // 5. 保存到会话表
    session_dispatchers_[session_id] = session_dispatcher;

    // 6. 创建心跳线程：每5秒发心跳，保持连接活跃
    auto thread = std::make_unique<std::thread>([...]() {
        // 先发 endpoint 事件
        session_dispatcher->send_event("event: endpoint\r\ndata: " + session_uri + "\r\n\r\n");

        // 循环发心跳
        while (running_ && !session_dispatcher->is_closed()) {
            sleep(5秒);
            session_dispatcher->send_event("event: heartbeat\r\ndata: ...\r\n\r\n");
        }
        close_session(session_id);
    });
    sse_threads_[session_id] = std::move(thread);

    // 7. 设置 chunked 响应 -- 告诉 httplib 保持连接
    res.set_chunked_content_provider("text/event-stream",
        [session_dispatcher](size_t offset, httplib::DataSink& sink) {
            // 这个 lambda 会被反复调用
            // 阻塞等待新事件
            bool result = session_dispatcher->wait_event(&sink);
            if (!result) {
                close_session(session_id);
                return false;
            }
            return true;
        });
}
```

### 4.2 建立流程图

```
Client                          Server                    event_dispatcher
  |                               |                          |
  |--- GET /sse ---------------->| handle_sse()             |
  |                               |  + generate session_id  |
  |                               |  + create dispatcher    |
  |                               |  + save to map           |
  |                               |  + spawn heartbeat thread|
  |                               |  + set_chunked_provider |
  |                               |            |             |
  |<-- 200 OK (chunked) ----------|            |             |
  |    Content-Type: text/event-stream         v             |
  |                               |  wait_event() [blocking]|
  |                               |            |             |
  |<-- event: endpoint -----------|  (heartbeat thread sends)|
  |    data: /message?session_id=xxx                         |
  |                               |                          |
  |  [SSE 长连接建立完毕，持续保持] |                          |
```

### 4.3 一个 Client 对应一个 event_dispatcher

Server 维护一个 map：session_id -> event_dispatcher 的映射。

```
session_dispatchers_:
  abc123 -> dispatcher_A  (Client A 的 SSE 连接)
  def456 -> dispatcher_B  (Client B 的 SSE 连接)
  ghi789 -> dispatcher_C  (Client C 的 SSE 连接)
```

---

## 5. event_dispatcher：生产者-消费者机制

event_dispatcher 是核心同步机制，实现"等待-通知"模式。SSE 连接（消费者）阻塞等待，线程池处理完请求（生产者）发送通知。

### 5.1 源码

```cpp
// include/mcp_server.h 第45-167行
class event_dispatcher {
private:
    mutable std::mutex m_;                    // 互斥锁
    std::condition_variable cv_;             // 条件变量
    std::atomic<int> id_{0};                 // 消息版本号
    std::atomic<int> cid_{-1};               // 已消费版本号
    std::string message_;                   // 消息内容
    std::atomic<bool> closed_{false};        // 关闭标志

public:
    // 消费者：等待新事件（阻塞）
    bool wait_event(httplib::DataSink* sink, ...) {
        std::unique_lock<std::mutex> lk(m_);
        // 等待条件：id != cid（有新消息） 或 关闭
        cv_.wait_for(lk, timeout, [&] {
            return cid_ != id_ || closed_;
        });
        if (closed_) return false;
        // 写入 HTTP 响应
        sink->write(message_.data(), message_.size());
        return true;
    }

    // 生产者：发送事件（非阻塞）
    bool send_event(const std::string& message) {
        std::lock_guard<std::mutex> lk(m_);
        message_ = message;
        cid_.store(id_.fetch_add(1));  // cid_ = 旧的 id_, id_++
        cv_.notify_one();  // 唤醒一个等待的线程
        return true;
    }

    void close() {
        closed_.exchange(true);
        cv_.notify_all();  // 唤醒所有等待线程
    }
};
```

### 5.2 工作原理

```
1. SSE 连接初始化：
   wait_event() 进入阻塞，等待新消息

2. HTTP 请求到达，线程池处理：
   process_request() 处理完毕
   -> send_event(json_response)
   -> message_ = response, cid_++, notify_one()

3. SSE 连接被唤醒：
   wait_event() 返回
   -> 通过 sink->write() 把消息写入 HTTP 响应
   -> 客户端收到 "event: message\ndata: {result}"

4. wait_event() 继续阻塞，等待下一个事件
```

### 5.3 关键理解

- id_ 是"生产者最新版本号"，cid_ 是"消费者已消费版本号"
- cid_ != id_ 说明有新消息，消费者应该读取
- wait_event() 阻塞等待，不消耗 CPU
- send_event() 非阻塞，快速返回

---

## 6. handle_jsonrpc()：处理请求

### 6.1 源码

```cpp
// src/mcp_server.cpp 第803-916行
void server::handle_jsonrpc(const httplib::Request& req, httplib::Response& res) {
    // 1. 设置响应头
    res.set_header("Content-Type", "application/json");
    res.set_header("Access-Control-Allow-Origin", "*");

    // 2. 获取 session_id（从 URL 参数）
    std::string session_id = req.params.find("session_id")->second;

    // 3. 查找对应的 event_dispatcher
    std::shared_ptr<event_dispatcher> dispatcher;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = session_dispatchers_.find(session_id);
        if (it == session_dispatchers_.end()) {
            res.status = 404;  // Session 不存在
            return;
        }
        dispatcher = it->second;
    }

    // 4. 解析 JSON 请求
    json req_json = json::parse(req.body);

    // 5. 构建 MCP 请求对象
    request mcp_req;
    mcp_req.jsonrpc = req_json["jsonrpc"];
    mcp_req.id = req_json.value("id", nullptr);
    mcp_req.method = req_json["method"];
    mcp_req.params = req_json.value("params", json::object());

    // 6. 如果是 Notification（无 id），直接异步处理，不返回响应
    if (mcp_req.is_notification()) {
        thread_pool_.enqueue([this, mcp_req, session_id]() {
            process_request(mcp_req, session_id);
        });
        res.status = 202;
        return;
    }

    // 7. 提交到线程池异步处理
    thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
        // 7a. 处理请求
        json response_json = process_request(mcp_req, session_id);

        // 7b. 通过 SSE 推送响应
        std::stringstream ss;
        ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
        dispatcher->send_event(ss.str());
    });

    // 8. 立即返回 202 Accepted
    res.status = 202;
    res.set_content("Accepted", "text/plain");
}
```

### 6.2 流程图

```
Client POST /message (with session_id)          Server
  |                                                |
  | JSON-RPC body: {"jsonrpc":"2.0",              |
  |   "id":1,"method":"tools/call",...}           |
  |----------------------------------------------->| handle_jsonrpc()
  |                                                |  + Get session_id
  |                                                |  + Find dispatcher
  |                                                |  + Parse JSON
  |                                                |  + thread_pool.enqueue(task)
  |<-- 202 Accepted -------------------------------|
  |                                                |
  |                                                | [Thread Pool]
  |                                                |  + process_request()
  |                                                |  + call tool handler
  |                                                |  + get result
  |                                                |  + dispatcher.send_event()
  |                                                |      -> notify_one()
  |                                                |      -> wait_event() wakes up
  |<-- event: message -----------------------------|
  |    data: {"jsonrpc":"2.0","id":1,"result":{...}|
  |                                                |
```

### 6.3 线程池

```cpp
// 提交任务到线程池
thread_pool_.enqueue([this, mcp_req, session_id, dispatcher]() {
    json response_json = process_request(mcp_req, session_id);
    std::stringstream ss;
    ss << "event: message\r\ndata: " << response_json.dump() << "\r\n\r\n";
    dispatcher->send_event(ss.str());
});
```

线程池特点：任务多时自动创建新线程，空闲时回收线程，防止任务堆积。

---

## 7. process_request()：核心处理逻辑

无论 HTTP/SSE 模式还是 stdio 模式，**都使用同一个 process_request()**。

```cpp
// src/mcp_server.cpp 第1187-1259行
json server::process_request(const request& req, const std::string& session_id) {
    // Notification（无 id）：不返回响应
    if (req.is_notification()) {
        if (req.method == "notifications/initialized") {
            set_session_initialized(session_id, true);
        }
        return json::object();
    }

    try {
        LOG_INFO("Processing method call: ", req.method);

        // 特殊方法：initialize
        if (req.method == "initialize") {
            return handle_initialize(req, session_id);
        }
        // 特殊方法：ping
        else if (req.method == "ping") {
            return response::create_success(req.id, json::object()).to_json();
        }

        // 检查会话是否已初始化
        if (!is_session_initialized(session_id)) {
            return response::create_error(
                req.id, error_code::invalid_request, "Session not initialized"
            ).to_json();
        }

        // 查找注册的 handler
        method_handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = method_handlers_.find(req.method);
            if (it != method_handlers_.end()) {
                handler = it->second;
            }
        }

        // 调用 handler
        if (handler) {
            json result = handler(req.params, session_id);
            return response::create_success(req.id, result).to_json();
        }

        // 方法未找到
        return response::create_error(
            req.id, error_code::method_not_found,
            "Method not found: " + req.method
        ).to_json();

    } catch (const mcp_exception& e) {
        return response::create_error(req.id, e.code(), e.what()).to_json();
    }
}
```

### 7.2 和 stdio 模式的区别

| 对比项 | stdio 模式 | HTTP/SSE 模式 |
|--------|-----------|---------------|
| 入口 | start_stdio() | start(true) |
| 监听 | stdin/stdout | httplib 监听端口 |
| 请求读取 | std::getline(std::cin) | handle_jsonrpc() |
| 响应返回 | std::cout + flush | dispatcher->send_event() |
| 核心处理 | process_request() | process_request() (相同) |
| 并发 | 单线程串行 | 线程池并行 |
| Session | 无 | 每连接独立 session_id + dispatcher |

---

## 8. Client 端：sse_client 怎么工作？

### 8.1 代码示例

```cpp
// examples/sse_client_example.cpp
int main() {
    // 1. 创建 SSE Client
    mcp::sse_client client("http://localhost:8888");

    // 2. 初始化连接（发送 GET /sse + POST /message initialize）
    client.initialize("ExampleClient", mcp::MCP_VERSION);

    // 3. 调用工具
    mcp::json result = client.call_tool("echo", {{"text", "Hello"}});
    cout << result["content"][0]["text"] << endl;

    // 4. 获取工具列表
    auto tools = client.get_tools();
    for (auto& t : tools) cout << t.name << endl;
}
```

### 8.2 连接流程

sse_client 初始化时做了三件事：
1. 发送 GET /sse 建立 SSE 长连接
2. 启动独立线程循环读取 SSE 事件流
3. 发送 POST /message (initialize) 完成初始化

调用工具时：
1. 发送 POST /message (tools/call)
2. 在 SSE 流中等待响应事件
3. 收到后返回结果

### 8.3 类结构

```cpp
// include/mcp_sse_client.h
class sse_client : public client {
    std::string server_url_;                  // http://localhost:8888
    std::unique_ptr<httplib::Client> http_client_;  // HTTP 客户端
    std::thread sse_thread_;                  // SSE 读取线程
    std::atomic<bool> sse_running_{false};
};
```

---

## 9. 完整通信时序流程

### 9.1 初始化阶段

```
Step 1: Client 建立 SSE 连接
  Client                         Server
    |--- GET /sse -------------->| handle_sse()
    |<-- 200 OK (chunked) -------|  + generate session_id
    |                            |  + create event_dispatcher
    |<-- event: endpoint --------|  (heartbeat thread sends)
    |    data: session_uri       |

Step 2: Client 发送 initialize 请求
  Client                         Server              Thread Pool
    |--- POST /message --------->| handle_jsonrpc()
    |   {"method":"initialize"}  |  + thread_pool.enqueue()
    |<-- 202 Accepted -----------|                    |
    |                            |                    v
    |                            |              process_request()
    |                            |                    |
    |                            |                    v
    |                            |              handle_initialize()
    |                            |                    |
    | (wait_event wakes up)      |                    v
    |<-- event: message ---------|              dispatcher.send_event()
    |    data: {capabilities...} |

Step 3: Client 发送 initialized 通知
  Client                         Server              Thread Pool
    |--- POST /message --------->| handle_jsonrpc()
    |   {"method":"notifications/|  + thread_pool.enqueue()
    |    initialized"}           |                    |
    |<-- 202 Accepted -----------|                    v
    |                            |              process_request()
    |                            |              set_session_initialized(true)

  *** 初始化完成！***
```

### 9.2 工具调用阶段

```
  Client                         Server              Thread Pool
    |                            |                    |
    | client.call_tool("echo")   |                    |
    |  + POST /message           |                    |
    |  + wait for response       |                    |
    |--- POST /message --------->| handle_jsonrpc()  |
    |   {"method":"tools/call",  |  + parse JSON      |
    |    "params":{...}}         |  + find dispatcher |
    |<-- 202 Accepted -----------|  + thread_pool     |
    |                            |      .enqueue()--->|
    |                            |                    |
    |                            |                    v
    |                            |              process_request()
    |                            |                    |
    |                            |                    v
    |                            |              call echo_handler()
    |                            |              return result
    |                            |                    |
    |                            |                    v
    |                            |              dispatcher.send_event()
    |                            |                    |
    |  (wait_event wakes up)     |                    |
    |<-- event: message ---------|                    |
    |    data: {"result":{...}}  |                    |
    |                            |                    |
    |  *** 工具调用完成！***      |                    |
```

### 9.3 关闭流程

```
  Client                         Server
    |                            |
    | 1. Client 退出 / 关闭连接  |
    |                            |
    |                            | 2. SSE 连接断开
    |                            |    chunked provider 返回 false
    |                            |       |
    |                            |       v
    |                            |  close_session(session_id)
    |                            |   + 从 map 删除
    |                            |   + 关闭 dispatcher
    |                            |   + 等待心跳线程退出
    |                            |
    |                            | *** Session 清理完成！***
```

---

## 10. 总结

两种模式的核心对比：

| 对比项 | stdio 模式 | HTTP/SSE 模式 |
|--------|-----------|---------------|
| 启动方式 | server.start_stdio() | server.start(true) |
| 通信方式 | 管道 stdin/stdout | HTTP + SSE 长连接 |
| 并发能力 | 单客户端串行 | 多客户端并行 |
| 请求入口 | std::getline() | handle_jsonrpc() |
| 响应返回 | std::cout + flush | dispatcher.send_event() |
| 核心处理 | process_request() | process_request() (相同) |
| Session 管理 | 无 | 每连接独立 session_id + dispatcher |
| 适用场景 | OpenCode / Claude Desktop | Web / 远程调用 |

**共同点**：两种模式的业务逻辑完全相同，都是通过 process_request() 处理！

---

## 11. 代码位置索引

| 功能 | 文件 | 行号 |
|------|------|------|
| Server start() | src/mcp_server.cpp | 120-214 |
| CORS 设置 | src/mcp_server.cpp | 127-134 |
| 路由注册 | src/mcp_server.cpp | 136-162 |
| handle_sse() | src/mcp_server.cpp | 684-801 |
| handle_jsonrpc() | src/mcp_server.cpp | 803-916 |
| process_request() | src/mcp_server.cpp | 1187-1259 |
| event_dispatcher 类 | include/mcp_server.h | 45-167 |
| 线程池实现 | include/mcp_thread_pool.h | 1-195 |
| SSE Client 示例 | examples/sse_client_example.cpp | 全文 |
| HTTP Server 示例 | examples/server_example.cpp | 全文 |