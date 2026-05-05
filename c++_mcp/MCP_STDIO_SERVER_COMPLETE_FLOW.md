# C++ MCP SDK Stdio 通信方式完整工作流程

## 概述

本文档详细讲解基于 Stdio 管道通信的 MCP Server 全流程，包括：
1. Server 端：用户编写什么代码
2. Server 端：框架 `start_stdio()` 怎么工作
3. Server 端：`process_request()` 怎么处理请求
4. Client 端：OpenCode/stdio_client 怎么启动子进程
5. Client 端：怎么通过管道收发数据
6. 完整通信时序流程

---

## 1. 整体架构

```
┌───────────────────────────────────────────────────────────────────────────────────────┐
│                          Stdio MCP 整体架构                                             │
└───────────────────────────────────────────────────────────────────────────────────────┘

  ┌────────────────────────┐                        ┌─────────────────────────────────┐
  │     Client 端          │                        │        Server 端                │
  │  (OpenCode / 我们的    │                        │  (用户写的 MCP Server 程序)     │
  │   stdio_client)        │                        │                                 │
  │                        │                        │   main() {                      │
  │  stdio_client:         │                        │     server server(config);      │
  │    CreatePipe()        │      stdin 管道         │     register_tool(...);         │
  │    STARTUPINFO         │ ═══════════════════════►│     register_prompt(...);       │
  │    CreateProcess()     │                         │     register_resource(...);      │
  │                        │      stdout 管道         │     start_stdio();   ← 我们加的  │
  │    send_jsonrpc()      │ ═══════════════════════►│   }                            │
  │    read_thread_func() │◄═══════════════════════│                                 │
  └────────────────────────┘                       └─────────────────────────────────┘
```

---

## 2. Server 端：用户怎么写代码

### 2.1 完整的 Server 代码示例

以 `my_mcp_server/main.cpp` 为例：

```cpp
// === 第1步：创建 Server 实例 ===
mcp::server::configuration srv_conf;
srv_conf.host = "localhost";
srv_conf.port = 8888;

mcp::server server(srv_conf);
server.set_server_info("PKB_Assistant", "1.0.0");
server.set_capabilities({
    {"tools", mcp::json::object()},
    {"prompts", mcp::json::object()},
    {"resources", mcp::json::object()}
});

// === 第2步：注册 Tools ===
mcp::tool list_notes_tool = mcp::tool_builder("list_notes")
    .with_description("List all available markdown notes in the knowledge base.")
    .build();

server.register_tool(list_notes_tool, list_notes_handler);
server.register_tool(search_notes_tool, search_notes_handler);
server.register_tool(create_note_tool, create_note_handler);

// === 第3步：注册 Resources ===
auto file_resource = std::make_shared<mcp::file_resource>(file_path);
server.register_resource("file://knowledge_base.md", file_resource);

// === 第4步：注册 Prompts ===
mcp::prompt draft_article_prompt = mcp::prompt_builder("draft_technical_article")
    .with_description("An agent skill prompt to draft a new technical article.")
    .with_argument("topic", "The main topic to write about", true)
    .build();

server.register_prompt(draft_article_prompt, [](const json& args, const string& session_id) -> json {
    // prompt handler 逻辑
    return mcp::json::array({message});
});

// === 第5步：启动 Stdio 模式！ ===
server.start_stdio();    // ← 阻塞等待，监听 stdin
```

---

## 3. Server 端：start_stdio() 怎么工作？

### 3.1 源码

```cpp
// src/mcp_server.cpp 第56-124行
void server::start_stdio() {
    running_ = true;
    std::string line;
    std::string session_id = "stdio_session_" + std::to_string(std::time(nullptr));
    
    // ── 核心循环：逐行读取 stdin ──
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;
        
        try {
            // 1. 解析 JSON-RPC 请求
            json req_json = json::parse(line);
            request req;
            
            // 2. 检查 JSON-RPC 2.0 格式
            if (req_json.is_object() && req_json.contains("jsonrpc") && req_json["jsonrpc"] == "2.0") {
                req.jsonrpc = "2.0";
                req.id = req_json.contains("id") ? req_json["id"] : nullptr;
                req.method = req_json.value("method", "");
                if (req_json.contains("params")) {
                    req.params = req_json["params"];
                }
                
                // 3. 调用核心处理函数（和 HTTP 模式一样！）
                json res = process_request(req, session_id);
                
                // 4. 输出响应到 stdout
                if (!res.is_null() && !req.id.is_null()) {
                    std::cout << res.dump() << "\n" << std::flush;
                } else {
                    if (!res.is_null()) {
                        std::cout << res.dump() << "\n" << std::flush;
                    }
                }
            } else {
                // 无效格式，返回错误
                json err_res = {
                    {"jsonrpc", "2.0"},
                    {"error", {
                        {"code", static_cast<int>(error_code::invalid_request)},
                        {"message", "Invalid JSON-RPC format"}
                    }}
                };
                err_res["id"] = req_json.contains("id") ? req_json["id"] : nullptr;
                std::cout << err_res.dump() << "\n" << std::flush;
            }
        } catch (const std::exception& e) {
            // 解析错误
            json err_res = {
                {"jsonrpc", "2.0"},
                {"error", {
                    {"code", static_cast<int>(error_code::parse_error)},
                    {"message", std::string("Parse error: ") + e.what()}
                }},
                {"id", nullptr}
            };
            std::cout << err_res.dump() << "\n" << std::flush;
        }
    }
    // 管道关闭，while 退出
    running_ = false;
}
```

### 3.2 流程解析

```
start_stdio()
     │
     ▼
  std::getline(std::cin, line)   ← 阻塞等数据
     │
     │ 管道里有数据了
     ▼
  json::parse(line)              ← 解析 JSON
     │
     ▼
  构造 request 对象               ← 组装 method/params/id
     │
     ▼
  process_request(req, session_id) ← 核心处理（和 HTTP 一样！）
     │
     ▼
  std::cout << res.dump()        ← 输出响应
      + std::flush               ← 确保立即发送
     │
     ▼
  回到循环开头，继续阻塞等待       ← 长连接保持
```

---

## 4. Server 端：process_request() 怎么处理请求？

### 4.1 源码

```cpp
// src/mcp_server.cpp 第1187-1259行
json server::process_request(const request& req, const std::string& session_id) {
    // 如果是 Notification（无 ID），不需要返回响应
    if (req.is_notification()) {
        if (req.method == "notifications/initialized") {
            set_session_initialized(session_id, true);
        }
        return json::object();
    }
    
    try {
        LOG_INFO("Processing method call: ", req.method);
        
        // === 特殊方法：initialize ===
        if (req.method == "initialize") {
            return handle_initialize(req, session_id);  // 返回协议版本、能力等
        }
        // === 特殊方法：ping ===
        else if (req.method == "ping") {
            return response::create_success(req.id, json::object()).to_json();
        }
        
        // === 检查会话是否已初始化 ===
        if (!is_session_initialized(session_id)) {
            return response::create_error(
                req.id, 
                error_code::invalid_request, 
                "Session not initialized"
            ).to_json();
        }
        
        // === 查找注册的 handler ===
        method_handler handler;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = method_handlers_.find(req.method);
            if (it != method_handlers_.end()) {
                handler = it->second;
            }
        }
        
        // === 调用 handler ===
        if (handler) {
            LOG_INFO("Calling method handler: ", req.method);
            json result = handler(req.params, session_id);
            return response::create_success(req.id, result).to_json();
        }
        
        // === 方法未找到 ===
        return response::create_error(
            req.id,
            error_code::method_not_found,
            "Method not found: " + req.method
        ).to_json();
        
    } catch (const mcp_exception& e) {
        return response::create_error(req.id, e.code(), e.what()).to_json();
    } catch (const std::exception& e) {
        return response::create_error(
            req.id, error_code::internal_error, 
            "Internal error: " + std::string(e.what())
        ).to_json();
    }
}
```

### 4.2 handler 是怎么注册的？

```cpp
// 用户调用 register_tool() 时，框架内部会注册到 method_handlers_ map 里：

// mcp_server.cpp 中 register_tool 的简化内部逻辑
void server::register_tool(const tool& tool, tool_handler handler) {
    // 把 tools/list 映射到内部的处理函数
    // 把 tools/call 映射到内部的处理函数（带 handler 参数）
    
    // tools/list: 返回所有已注册工具的列表
    // tools/call: 根据 name 查找对应的 handler 并调用
}
```

### 4.3 process_request 处理流程图

```
process_request(req, session_id)
          │
          ▼
  req.method 是什么？
          │
    ┌─────┼─────┬──────────────┬──────────────┐
    ▼     ▼     ▼              ▼              ▼
  init  ping  tools/list   tools/call     prompts/get
    │     │        │              │              │
    ▼     ▼        ▼              ▼              ▼
 handle_ 创建     返回所有      查找对应      返回所有
 initialize 空响应  已注册工具    handler 并调用 已注册 prompt
```

---

## 5. Client 端：OpenCode/stdio_client 怎么启动子进程？

### 5.1 调用链

```
用户代码 (stdio_client_example.cpp):
────────────────────────────────────────────────────────────────────────

    mcp::stdio_client client("my_mcp_server.exe");
    client.initialize("MyClient", "1.0.0");  
                    │
                    ▼

initialize() 内部 (mcp_stdio_client.cpp:40):
────────────────────────────────────────────────────────────────────────

    bool stdio_client::initialize(...) {
        start_server_process();    // ← 启动子进程！
        ...
        send_jsonrpc(initialize_req);  // 发送 initialize 请求
        ...
    }
                    │
                    ▼

start_server_process() (mcp_stdio_client.cpp:200):
────────────────────────────────────────────────────────────────────────
    创建管道 + 启动子进程
```

### 5.2 start_server_process() 完整源码

```cpp
// src/mcp_stdio_client.cpp 第200-328行

bool stdio_client::start_server_process() {
    if (running_) return true;
    
    // ═══ 第1步：创建安全属性（允许子进程继承句柄） ═══
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;    // ← 关键！
    sa.lpSecurityDescriptor = NULL;

    // ═══ 第2步：创建 stdin 管道 ═══
    HANDLE child_stdin_read = NULL;
    HANDLE child_stdin_write = NULL;
    CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0);
    
    // stdio_client 保留写端（用它给子进程发数据）
    // 子进程获取读端（用它接收数据）
    SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0);
    
    // ═══ 第3步：创建 stdout 管道 ═══
    HANDLE child_stdout_read = NULL;
    HANDLE child_stdout_write = NULL;
    CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0);
    
    // 子进程获取写端（用它发送数据）
    // stdio_client 保留读端（用它接收数据）
    SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0);

    // ═══ 第4步：配置 STARTUPINFO（告诉系统用自定义 stdin/stdout） ═══
    STARTUPINFOA si;
    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);
    si.hStdInput  = child_stdin_read;    // 子进程的 stdin 绑定到此管道
    si.hStdOutput = child_stdout_write;   // 子进程的 stdout 绑定到此管道
    si.hStdError  = child_stdout_write;   // 子进程的 stderr 也绑定到此
    si.dwFlags   |= STARTF_USESTDHANDLES; // 告诉系统使用自定义句柄

    // ═══ 第5步：启动子进程 ═══
    PROCESS_INFORMATION pi;
    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    CreateProcessA(
        NULL,              // 应用程序名
        cmd_line,          // 命令行
        NULL, NULL,        // 安全属性
        TRUE,              // 继承句柄 = TRUE
        CREATE_NO_WINDOW,  // 不创建窗口
        NULL, NULL,        // 环境变量、工作目录
        &si,               // 启动信息（包含管道句柄）
        &pi                // 返回进程信息
    );

    // ═══ 第6步：关闭不需要的句柄 ═══
    CloseHandle(child_stdin_read);    // 父进程不需要读自己的 stdin
    CloseHandle(child_stdout_write);  // 父进程不需要写自己的 stdout

    // ═══ 第7步：保存句柄用于后续通信 ═══
    stdin_pipe_[0] = NULL;              // 父进程读端（不需要）
    stdin_pipe_[1] = child_stdin_write; // 父进程写端 → 给子进程发消息
    stdout_pipe_[0] = child_stdout_read;// 父进程读端 ← 从子进程收消息
    stdout_pipe_[1] = NULL;             // 父进程写端（不需要）

    // ═══ 第8步：启动读取线程 ═══
    running_ = true;
    read_thread_ = std::make_unique<std::thread>(&stdio_client::read_thread_func, this);
    
    return true;
}
```

### 5.3 管道句柄分配图解

```
CreatePipe() 之后：

  stdin 管道：
  ┌──────────────────┐     管道     ┌──────────────────┐
  │ child_stdin_read │◄══════════►│ child_stdin_write│
  │   (读端)          │              │   (写端)          │
  └────────┬─────────┘              └────────┬─────────┘
           │                                 │
           ▼                                 ▼
    传给子进程(stdin)                  父进程保留(stdin_pipe_[1])
    子进程: std::cin 读这里           父进程: WriteFile 写这里


  stdout 管道：
  ┌──────────────────┐     管道     ┌──────────────────┐
  │ child_stdout_read│◄══════════►│child_stdout_write│
  │   (读端)          │              │   (写端)          │
  └────────┬─────────┘              └────────┬─────────┘
           │                                 │
           ▼                                 ▼
    父进程保留(stdout_pipe_[0])         传给子进程(stdout)
    父进程: ReadFile 读这里            子进程: std::cout 写这里
```

---

## 6. Client 端：怎么通过管道收发数据？

### 6.1 发送请求：send_jsonrpc()

```cpp
// src/mcp_stdio_client.cpp 第782-812行
json stdio_client::send_jsonrpc(const request& req) {
    if (!running_) {
        throw mcp_exception(error_code::internal_error, "Server process not running");
    }
    
    // 1. 序列化为 JSON 字符串
    json req_json = req.to_json();
    std::string req_str = req_json.dump() + "\n";  // ← 注意加 \n
    
    // 2. 写入 stdin 管道（发给子进程）
    #if defined(_WIN32)
    DWORD bytes_written;
    WriteFile(stdin_pipe_[1],        // ← 写端句柄
              req_str.c_str(), 
              static_cast<DWORD>(req_str.size()), 
              &bytes_written, NULL);
    #else
    write(stdin_pipe_[1], req_str.c_str(), req_str.size());
    #endif
    
    // 3. 如果是 Notification（无 ID），不需要等响应
    if (req.is_notification()) {
        return json::object();
    }
    
    // 4. 创建 Promise/Future，等待响应
    std::promise<json> response_promise;
    std::future<json> response_future = response_promise.get_future();
    {
        std::lock_guard<std::mutex> lock(response_mutex_);
        pending_requests_[req.id] = std::move(response_promise);
    }
    
    // 5. 阻塞等待响应（最多60秒）
    auto timeout = std::chrono::seconds(60);
    auto status = response_future.wait_for(timeout);
    
    if (status == std::future_status::ready) {
        return response_future.get();    // 返回结果
    } else {
        throw mcp_exception(error_code::internal_error, "Request timeout");
    }
}
```

### 6.2 接收响应：read_thread_func()

```cpp
// src/mcp_stdio_client.cpp 第582-780行
void stdio_client::read_thread_func() {
    char buffer[4096];
    std::string data_buffer;
    
    while (running_) {
    
        // 1. 从 stdout 管道读取数据
        DWORD bytes_read;
        ReadFile(stdout_pipe_[0],   // ← 读端句柄
                 buffer, 
                 4095, 
                 &bytes_read, NULL);
        
        // 2. 拼接到缓冲区
        buffer[bytes_read] = '\0';
        data_buffer.append(buffer, bytes_read);
        
        // 3. 按 \n 分割，逐条处理 JSON-RPC 消息
        size_t pos;
        while ((pos = data_buffer.find('\n')) != std::string::npos) {
            std::string line = data_buffer.substr(0, pos);
            data_buffer.erase(0, pos + 1);
            
            // 4. 解析 JSON
            json message = json::parse(line);
            
            // 5. 检查是否是响应（有 jsonrpc + id）
            if (message.contains("jsonrpc") && 
                message["jsonrpc"] == "2.0" &&
                message.contains("id") && 
                !message["id"].is_null()) {
                
                json id = message["id"];
                
                // 6. 找到对应的 Promise（通过之前保存的 pending_requests_）
                std::lock_guard<std::mutex> lock(response_mutex_);
                auto it = pending_requests_.find(id);
                
                if (it != pending_requests_.end()) {
                    // 7. 设置 Promise 的值，唤醒阻塞的 send_jsonrpc()
                    if (message.contains("result")) {
                        it->second.set_value(message["result"]);
                    } else if (message.contains("error")) {
                        json error_result = {
                            {"isError", true},
                            {"error", message["error"]}
                        };
                        it->second.set_value(error_result);
                    }
                    pending_requests_.erase(it);
                }
            }
        }
    }
}
```

### 6.3 收发数据的 Promise/Future 机制

```
send_jsonrpc()                            read_thread_func() (独立线程)
────────────                              ────────────────────
     │                                          │
     │ 1. WriteFile() 发送请求                    │
     ├────────────────────────────►              │
     │                                          │
     │ 2. pending_requests_[id] = promise        │
     │                                          │
     │ 3. future.wait_for(60s) [阻塞]              │
     │                                          │
     │                             4. ReadFile() 收到响应
     │                             5. 解析 JSON，找到 id
     │                             6. pending_requests_[id].set_value(result)
     │                                          │
     │ 7. future 被唤醒，获取结果                  │
     │                                          │
     ▼                                          │
  return result                                 │
```

---

## 7. 完整通信时序流程

### 7.1 初始化阶段

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                初始化阶段                                                   │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  Client (OpenCode/stdio_client)              Server (子进程)
  ────────────────────────────────            ─────────────────
       │                                         │
       │ 1. CreatePipe() 创建两个管道              │
       │ 2. STARTUPINFO 设置管道句柄               │
       │ 3. CreateProcess() 启动子进程             │
       │    ├────────────────────────────────────►│ 子进程启动
       │                                         │
       │                                         │ 4. main() 开始
       │                                         │ 5. server server(config)
       │                                         │ 6. register_tool(...)
       │                                         │ 7. register_prompt(...)
       │                                         │ 8. register_resource(...)
       │                                         │ 9. start_stdio()
       │                                         │    │
       │                                         │    ▼
       │ 10. read_thread_ 启动（独立线程）          │ std::getline() [阻塞等待]
       │                                         │
       │                                         │
       │ 11. send_jsonrpc(initialize)            │
       │     WriteFile(stdin_pipe_[1], ...)      │
       │    ├──────────────────────────────────►│ std::getline() 返回
       │                                         │    │
       │                                         │    ▼
       │                                         │ 解析 JSON
       │                                         │    │
       │                                         │    ▼
       │                                         │ process_request(req, "stdio_session_...")
       │                                         │    │
       │                                         │    ├─ method == "initialize"
       │                                         │    └─ handle_initialize() 
       │                                         │        返回: protocolVersion, 
       │                                         │        capabilities, serverInfo
       │                                         │    │
       │                                         │    ▼
       │                                         │ std::cout << result << std::flush
       │    ◄──────────────────────────────────┤
       │     ReadFile(stdout_pipe_[0], ...)      │
       │                                         │
       │ 12. send_jsonrpc(initialized)           │
       │     WriteFile(stdin_pipe_[1], ...)      │
       │    ├──────────────────────────────────►│ std::getline() 返回
       │                                         │    │
       │                                         │    ▼
       │                                         │ process_request(...)
       │                                         │    is_notification() → true
       │                                         │    set_session_initialized(true)
       │                                         │    return {} (不返回给客户端)
       │                                         │
       │                                         │ std::getline() [继续阻塞]
       │                                         │
       │ ✅ 初始化完成！                             │
```

### 7.2 处理工具调用请求

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                             处理工具调用请求                                                 │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  Client (OpenCode/stdio_client)               Server (子进程)
  ────────────────────────────────             ─────────────────
       │                                          │
       │                                          │ std::getline() [阻塞等待]
       │                                          │
       │ 1. client.call_tool("create_note", {...})│
       │    → send_request("tools/call", {...}) │
       │    → send_jsonrpc(req)                 │
       │    → WriteFile(stdin_pipe_[1], ...)     │
       │   ┌───────────────────────────────────►│
       │   │ {"jsonrpc":"2.0",                    │ std::getline() 返回
       │   │  "id":3,                             │    │
       │   │  "method":"tools/call",              │    ▼
       │   │  "params":{"name":"create_note",     │ 解析 JSON
       │   │           "arguments":{...}}}       │    │
       │   │                                      │    ▼
       │   │                                      │ process_request(req, session_id)
       │   │                                      │    │
       │   │                                      │    ├─ method == "tools/call"
       │   │                                      │    ├─ 查找 method_handlers_
       │   │                                      │    ├─ 找到 tools/call 的 handler
       │   │                                      │    ├─ handler 内部根据 params.name
       │   │                                      │    │  找到 create_note_handler
       │   │                                      │    ├─ 调用 create_note_handler(params, sid)
       │   │                                      │    │
       │   │                                      │    │  create_note_handler:
       │   │                                      │    │    从 params 取 filename/content
       │   │                                      │    │    创建文件
       │   │                                      │    │    返回成功消息
       │   │                                      │    │
       │   │                                      │    ▼
       │   │                                      │ 组装响应: {"jsonrpc":"2.0",
       │   │                                      │            "id":3,
       │   │                                      │            "result":{...}}
       │   │                                      │    │
       │   │                                      │    ▼
       │   │                                      │ std::cout << result << std::flush
       │   ◄────────────────────────────────────│
       │     ReadFile(stdout_pipe_[0], ...)       │
       │    │                                      │
       │    ▼                                      │
       │  解析 JSON，找到 id=3                      │
       │  pending_requests_[3].set_value(result)   │
       │  future 被唤醒                               │
       │    │                                      │
       │    ▼                                      │
       │  return result                            │
       │                                          │
       │                                          │ std::getline() [继续阻塞]
       │                                          │
       │ ✅ 工具调用完成！                             │
```

### 7.3 关闭流程

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                 关闭流程                                                    │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  Client                                  管道                      Server
  ──────                                  ──                      ──────
     │                                     │                        │
     │ 1. OpenCode 退出 / 用户关闭              │                        │
     │                                     │                        │
     │ 2. stop_server_process()            │                        │
     │    CloseHandle(stdin_pipe_[1])      │                        │
     │    (关闭写端)                        │                        │
     │    ├────────────────────────────────┤                        │
     │                                     │ 管道关闭，发送 EOF        │
     │                                     │                        │
     │                                     │                        ▼
     │                                     │             std::getline() 返回 false
     │                                     │                        │
     │                                     │                        ▼
     │                                     │             while 循环退出
     │                                     │             running_ = false
     │                                     │                        │
     │                                     │                        ▼
     │                                     │             start_stdio() 返回
     │                                     │                        │
     │                                     │                        ▼
     │                                     │             main() 返回
     │                                     │                        │
     │                                     │                        ▼
     │                                     │             进程退出
     │                                     │
     │ 3. CloseHandle(stdout_pipe_[0])     │
     │    read_thread_ 检测到管道断开        │
     │    read_thread_func() 退出           │
     │                                     │
     │ 4. 等待进程退出 (waitpid/GetExitCode) │
     │                                     │
     │ ✅ 关闭完成！                             │
```

---

## 8. 代码位置索引

| 功能 | 文件位置 | 行号 |
|------|---------|------|
| Server start_stdio() | `src/mcp_server.cpp` | 56-124 |
| Server process_request() | `src/mcp_server.cpp` | 1187-1259 |
| Server handle_initialize() | `src/mcp_server.cpp` | 1270-1329 |
| Server 用户示例 | `my_mcp_server/main.cpp` | 124-241 |
| stdio_client 构造函数 | `src/mcp_stdio_client.cpp` | 30-34 |
| stdio_client::initialize() | `src/mcp_stdio_client.cpp` | 40-74 |
| start_server_process() | `src/mcp_stdio_client.cpp` | 200-328 |
| send_jsonrpc() | `src/mcp_stdio_client.cpp` | 782-840 |
| read_thread_func() | `src/mcp_stdio_client.cpp` | 582-780 |
| stop_server_process() | `src/mcp_stdio_client.cpp` | 430-580 |
| 客户端示例 | `examples/stdio_client_example.cpp` | 全文 |

---

## 9. 总结

1. **Server 端只需写 5 步**：创建 Server → 注册 Tool/Prompt/Resource → `start_stdio()`
2. **`start_stdio()` 核心逻辑**：`std::getline(std::cin)` 阻塞等待 → 解析 JSON → 调用 `process_request()` → `std::cout` 返回（+ `flush`）
3. **所有类型的请求都被路由到 `process_request()`**：它根据 `req.method` 来决定调用哪个 handler，不管是 stdio 还是 HTTP 都走这个逻辑
4. **Client 端通过 CreatePipe + STARTUPINFO + CreateProcess** 启动子进程并创建管道
5. **Client 通过 WriteFile/ReadFile 进行通信**，使用 Promise/Future 实现同步等待
6. **管道是单向的**：stdin 管道从 Client → Server（请求），stdout 管道从 Server → Client（响应）