# C++ MCP SDK Stdio 管道处理流程详解

## 概述

本文档详细讲解 MCP 服务器的 Stdio（标准输入输出）管道处理流程，包括：
1. 管道和 STARTUPINFO 基础概念
2. 客户端启动子进程的过程（CreateProcess + 管道）
3. 服务器端 start_stdio() 的实现
4. 完整的通信流程

---

## 1. 基础概念

### 1.1 管道 (Pipe) 是什么？

管道是操作系统提供的一种进程间通信机制，可以理解为"一根水管"：

```
┌─────────────────────────────────────────────────────────────┐
│                     管道 (Pipe) 概念                         │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   管道就像一根水管，有两个端口：                              │
│                                                             │
│        写入端                      读取端                   │
│      (write end)                (read end)                  │
│           │                          │                      │
│           ▼                          ▼                      │
│    ┌──────────────────────────────┐                        │
│    │         管道内部              │  数据从一端流向另一端    │
│    │                              │                        │
│    └──────────────────────────────┘                        │
│                                                             │
│   例子：                                                    │
│   • 自来水管：水流从水厂流向你家水龙头                       │
│   • 管道：数据从进程A流向进程B                              │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

**关键点**：每个管道需要两个句柄，一个读，一个写！

### 1.2 stdin/stdout 是什么？

每个进程默认有三个"标准流"：

```
┌─────────────────────────────────────────────────────────────┐
│                 进程的三个标准流                              │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   stdin  (标准输入)  - 默认绑定键盘输入                      │
│   stdout (标准输出)  - 默认绑定屏幕输出                      │
│   stderr (标准错误)  - 默认绑定屏幕输出                      │
│                                                             │
│   但可以通过"重定向"改变它们的绑定对象！                      │
│   比如绑定到管道、文件等                                     │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### 1.3 STARTUPINFO 结构体

`STARTUPINFO` 是 Windows API 的一个结构体，用来**配置新进程的启动方式**：

```cpp
typedef struct _STARTUPINFOA {
    DWORD cb;              // 结构体大小
    DWORD dwFlags;         // 标志位 ← 关键！告诉系统要自定义什么
    HANDLE hStdInput;      // ← 标准输入句柄
    HANDLE hStdOutput;     // ← 标准输出句柄
    HANDLE hStdError;      // ← 标准错误句柄
    // ... 其他字段
} STARTUPINFOA;
```

**关键字段**：
- `dwFlags`: 设置 `STARTF_USESTDHANDLES` 表示"我要自定义 stdin/stdout"
- `hStdInput`: 子进程的 stdin 要用这个句柄
- `hStdOutput`: 子进程的 stdout 要用这个句柄
- `hStdError`: 子进程的 stderr 要用这个句柄

---

## 2. 整体架构流程图

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                        Stdio 管道通信完整流程                              │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  ┌──────────────────┐     ┌──────────────────┐     ┌──────────────────┐
  │    OpenCode      │     │   操作系统        │     │   你的 Server    │
  │   (父进程)        │     │   (管道)          │     │   (子进程)       │
  │                  │     │                  │     │                  │
  │ • 读取配置       │     │                  │     │ • main()         │
  │ • 启动子进程     │     │                  │     │ • 注册工具       │
  │ • 发送/接收消息  │     │                  │     │ • start_stdio() │
  └────────┬─────────┘     └────────┬─────────┘     └────────┬─────────┘
           │                        │                        │
           │  1. CreateProcess      │                        │
           ├───────────────────────►│                        │
           │                        │                        │
           │  2. stdin 管道 ←───────┼────────────────────────┤ child_stdin_read
           │  3. stdout 管道 ───────┼────────────────────────► child_stdout_write
           │                        │                        │
           │                        │                        │
           │  WriteFile()           │                        │
           ├───────────────────────►│  ────────────────────►│ std::getline()
           │   JSON-RPC 请求        │    管道数据传输        │   读取 stdin
           │                        │                        │
           │                        │                        │
           │  ReadFile()           │                        │
           │◄──────────────────────│◄──────────────────────┤ std::cout
           │   JSON-RPC 响应        │    管道数据传输        │   写入 stdout
           │                        │                        │
           │                        │                        │
           │  [通信完成，长连接]     │                        │
```

---

## 3. OpenCode 启动子进程的过程

### 3.1 mcp_stdio_client.cpp 中的实现

这部分代码在 `src/mcp_stdio_client.cpp` 第 220-330 行：

```cpp
// src/mcp_stdio_client.cpp 第221-330行

#if defined(_WIN32)
    // Windows implementation

    // === 第1步：创建管道 ===
    SECURITY_ATTRIBUTES sa;
    sa.nLength = sizeof(SECURITY_ATTRIBUTES);
    sa.bInheritHandle = TRUE;  // 关键：允许子进程继承这个句柄
    sa.lpSecurityDescriptor = NULL;

    // 创建 stdin 管道
    // child_stdin_read  → 子进程读取端（保留）
    // child_stdin_write → 子进程写入端（关闭，因为只用于输入）
    if (!CreatePipe(&child_stdin_read, &child_stdin_write, &sa, 0)) {
        LOG_ERROR("Failed to create stdin pipe: ", GetLastError());
        return false;
    }

    // 创建 stdout 管道
    // child_stdout_read → 子进程读取端（关闭，因为只用于输出）
    // child_stdout_write → 子进程写入端（保留）
    if (!CreatePipe(&child_stdout_read, &child_stdout_write, &sa, 0)) {
        LOG_ERROR("Failed to create stdout pipe: ", GetLastError());
        return false;
    }

    // === 第2步：设置管道句柄不可继承 ===
    // 子进程只需要继承读端（stdin）和写端（stdout），父进程保留相反的端
    if (!SetHandleInformation(child_stdin_write, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        return false;
    }
    if (!SetHandleInformation(child_stdout_read, HANDLE_FLAG_INHERIT, 0)) {
        CloseHandle(child_stdin_read);
        CloseHandle(child_stdin_write);
        CloseHandle(child_stdout_read);
        CloseHandle(child_stdout_write);
        return false;
    }

    // === 第3步：配置 STARTUPINFO ===
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;

    ZeroMemory(&si, sizeof(STARTUPINFOA));
    si.cb = sizeof(STARTUPINFOA);

    // 把管道的句柄传给子进程！
    si.hStdInput  = child_stdin_read;   // 子进程的 stdin 会读取这个
    si.hStdOutput = child_stdout_write; // 子进程的 stdout 会写入这个
    si.hStdError  = child_stdout_write; // 子进程的 stderr 也会写入这个

    // 告诉系统："我要用自定义的 stdin/stdout"
    si.dwFlags |= STARTF_USESTDHANDLES;

    ZeroMemory(&pi, sizeof(PROCESS_INFORMATION));

    // === 第4步：创建子进程 ===
    std::string cmd_line = "cmd.exe /c " + command_;

    BOOL success = CreateProcessA(
        NULL,                         // 程序名（用命令行指定）
        cmd_line.c_str(),             // 命令行
        NULL,                         // 进程安全属性
        NULL,                         // 线程安全属性
        TRUE,                         // 继承句柄 = true（关键！）
        CREATE_NO_WINDOW,             // 不显示窗口
        NULL,                         // 环境变量
        NULL,                         // 当前目录
        &si,                          // 启动信息（包含管道句柄！）
        &pi                           // 返回进程信息
    );

    // === 第5步：关闭不需要的句柄 ===
    // 子进程已经继承了需要的句柄，父进程关闭自己的副本
    CloseHandle(child_stdin_read);   // 父进程不需要读自己的 stdin
    CloseHandle(child_stdout_write); // 父进程不需要写自己的 stdout
    CloseHandle(pi.hThread);

    // === 第6步：保存句柄用于后续通信 ===
    // stdin_pipe_[0] = 父进程读端（不需要，设为 NULL）
    // stdin_pipe_[1] = 父进程写端 → 用来给子进程发消息
    stdin_pipe_[0] = NULL;
    stdin_pipe_[1] = child_stdin_write;

    // stdout_pipe_[0] = 父进程读端 → 用来收子进程的消息
    // stdout_pipe_[1] = 父进程写端（不需要，设为 NULL）
    stdout_pipe_[0] = child_stdout_read;
    stdout_pipe_[1] = NULL;

    // 设置非阻塞模式
    DWORD mode = PIPE_NOWAIT;
    SetNamedPipeHandleState(stdout_pipe_[0], &mode, NULL, &timeout);
```

### 3.2 管道创建详细流程

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                         管道创建详细流程                                            │
└─────────────────────────────────────────────────────────────────────────────────────┘

  步骤1: CreatePipe() 创建管道
  ┌───────────────────────────────────────────────────────────────────────────────┐
  │                                                                                │
  │   CreatePipe(&hRead, &hWrite, &sa, 0)                                         │
  │                                                                                │
  │   ┌───────────────────┐         ┌───────────────────┐                       │
  │   │  child_stdin_read │◄────────►│ child_stdin_write │                       │
  │   │  (句柄)            │  stdin   │  (句柄)            │                       │
  │   └───────────────────┘  管道    └───────────────────┘                       │
  │                                                                                │
  │   CreatePipe(&hRead, &hWrite, &sa, 0)                                        │
  │                                                                                │
  │   ┌───────────────────┐         ┌───────────────────┐                       │
  │   │ child_stdout_read │◄────────►│ child_stdout_write│                       │
  │   │  (句柄)            │ stdout  │  (句柄)            │                       │
  │   └───────────────────┘  管道   └───────────────────┘                        │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
  步骤2: SetHandleInformation() 设置不可继承
  ┌───────────────────────────────────────────────────────────────────────────────┐
  │                                                                                │
  │   SetHandleInformation(hWrite, HANDLE_FLAG_INHERIT, 0)                       │
  │                                                                                │
  │   意思是："这个句柄不要传给子进程"                                              │
  │                                                                                │
  │   stdin 管道：                                                                  │
  │   ├── child_stdin_read   → 可继承（子进程要用的）                              │
  │   └── child_stdin_write  → 不可继承（父进程自己用）                           │
  │                                                                                │
  │   stdout 管道：                                                                 │
  │   ├── child_stdout_read  → 不可继承（父进程自己用）                           │
  │   └── child_stdout_write → 可继承（子进程要用的）                             │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
  步骤3: 传递给子进程（通过 STARTUPINFO）
  ┌───────────────────────────────────────────────────────────────────────────────┐
  │                                                                                │
  │   si.hStdInput  = child_stdin_read;   // 子进程的 stdin                        │
  │   si.hStdOutput = child_stdout_write;  // 子进程的 stdout                       │
  │   si.hStdError  = child_stdout_write;  // 子进程的 stderr                      │
  │   si.dwFlags   |= STARTF_USESTDHANDLES;                                       │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
  步骤4: CreateProcess() 启动子进程
  ┌───────────────────────────────────────────────────────────────────────────────┐
  │                                                                                │
  │   CreateProcessA(..., &si, &pi);                                              │
  │                                                                                │
  │   系统会：                                                                      │
  │   1. 创建新进程                                                                 │
  │   2. 把 STARTUPINFO 里的句柄复制给子进程                                       │
  │   3. 子进程的 stdin 绑定到 child_stdin_read                                     │
  │   4. 子进程的 stdout 绑定到 child_stdout_write                                  │
  │                                                                                │
  │   效果：                                                                       │
  │   ┌─────────────────┐        stdin          ┌─────────────────┐              │
  │   │    父进程       │◄─────────────────────►│    子进程       │              │
  │   │                 │                        │                 │              │
  │   │ stdin_pipe_[1]──┼───── WriteFile() ─────►│  std::getline()│              │
  │   │  (写端)         │        管道            │  (读取 stdin)  │              │
  │   └─────────────────┘                        └─────────────────┘              │
  │                                                                                │
  │   ┌─────────────────┐        stdout         ┌─────────────────┐              │
  │   │    父进程       │◄─────────────────────►│    子进程       │              │
  │   │                 │                        │                 │              │
  │   │ stdout_pipe_[0]◄┼───── ReadFile() ──────┤  std::cout      │              │
  │   │  (读端)         │        管道            │  (写入 stdout) │              │
  │   └─────────────────┘                        └─────────────────┘              │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
```

### 3.3 句柄总结

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              句柄总结                                               │
└─────────────────────────────────────────────────────────────────────────────────────┘

  stdin 管道：
  ┌──────────────────┬──────────────┬─────────────────────────────────────────────┐
  │      句柄        │   用途       │                    说明                     │
  ├──────────────────┼──────────────┼─────────────────────────────────────────────┤
  │ child_stdin_read │ 传给子进程    │ 子进程用这个读取父进程发来的数据             │
  │ child_stdin_write│ 父进程保留    │ 父进程用这个写数据给子进程                   │
  │ stdin_pipe_[1]  │ 通信用       │ WriteFile(stdin_pipe_[1], ...) 发送消息      │
  └──────────────────┴──────────────┴─────────────────────────────────────────────┘

  stdout 管道：
  ┌───────────────────┬──────────────┬────────────────────────────────────────────┐
  │      句柄         │   用途       │                   说明                    │
  ├───────────────────┼──────────────┼────────────────────────────────────────────┤
  │ child_stdout_read │ 父进程保留    │ 父进程用这个读子进程发来的数据              │
  │ child_stdout_write│ 传给子进程    │ 子进程用这个写数据给父进程                  │
  │ stdout_pipe_[0]  │ 通信用       │ ReadFile(stdout_pipe_[0], ...) 接收消息    │
  └───────────────────┴──────────────┴────────────────────────────────────────────┘

  关键理解：
  • 管道是"单向"的！stdin 管道只能从父进程写到子进程
  • 所以需要两个管道：一个用于输入，一个用于输出
  • 每个管道的两端分别被父进程和子进程持有
```

---

## 4. 服务器端 start_stdio() 的实现

### 4.1 start_stdio() 函数概览

这部分代码在 `src/mcp_server.cpp` 第 56-124 行：

```cpp
// src/mcp_server.cpp 第56-124行
void server::start_stdio() {
    running_ = true;
    std::string line;
    std::string session_id = "stdio_session_" + std::to_string(std::time(nullptr));

    // 循环读取 stdin，直到管道关闭
    while (std::getline(std::cin, line)) {
        if (line.empty()) continue;

        try {
            // 1. 解析 JSON-RPC 请求
            json req_json = json::parse(line);
            request req;

            // 检查是否是有效的 JSON-RPC 2.0 消息
            if (req_json.is_object() && req_json.contains("jsonrpc") && req_json["jsonrpc"] == "2.0") {
                req.jsonrpc = "2.0";
                req.id = req_json.contains("id") ? req_json["id"] : nullptr;
                req.method = req_json.value("method", "");
                if (req_json.contains("params")) {
                    req.params = req_json["params"];
                }

                // 2. 处理请求（复用框架内部逻辑）
                json res = process_request(req, session_id);

                // 3. 发送响应到 stdout
                if (!res.is_null() && !req.id.is_null()) {
                    // 有 ID 的请求，需要返回响应
                    std::cout << res.dump() << "\n" << std::flush;
                } else {
                    // 通知类型（无 ID）或响应为 null
                    if (!res.is_null()) {
                        std::cout << res.dump() << "\n" << std::flush;
                    }
                }
            } else {
                // 无效的 JSON-RPC 格式
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

    running_ = false;
}
```

### 4.2 start_stdio() 详细流程

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                          start_stdio() 详细流程                                      │
└─────────────────────────────────────────────────────────────────────────────────────┘

  start_stdio()
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 1. 初始化                                                                │
  │    running_ = true                                                        │
  │    生成 session_id                                                        │
  └───────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 2. 阻塞读取 stdin（关键！）                                              │
  │                                                                                │
  │    while (std::getline(std::cin, line)) {                                    │
  │        // 这个调用会阻塞，直到：                                            │
  │        // - 管道另一端（OpenCode）写入数据                                  │
  │        // - 管道被关闭（OpenCode 退出）                                      │
  │        // - 发生错误                                                         │
  │    }                                                                       │
  │                                                                                │
  │    ┌─────────────────┐        stdin          ┌─────────────────┐            │
  │    │    OpenCode     │──────────────────────►│    Server      │            │
  │    │                 │      管道写入          │                 │            │
  │    │ WriteFile()    │──────────────────────►│ std::getline() │            │
  │    │ (发送 JSON-RPC) │                        │   阻塞等待     │            │
  │    └─────────────────┘                        └─────────────────┘            │
  └───────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 3. 解析 JSON-RPC 请求                                                      │
  │                                                                                │
  │    json req_json = json::parse(line);                                        │
  │                                                                                │
  │    解析成功                                                                  │
  │         │                                                                    │
  │    ┌────┴────┐                                                               │
  │    │         │                                                               │
  │    ▼         ▼                                                               │
  │ 有效 JSON-RPC   无效格式                                                     │
  │    │         │                                                               │
  │    ▼         ▼                                                               │
  │ 继续处理    返回错误                                                         │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 4. 调用框架内部处理逻辑                                                    │
  │                                                                                │
  │    json res = process_request(req, session_id);                              │
  │                                                                                │
  │    这个方法会：                                                               │
  │    ├── 解析 method（initialize, tools/list, tools/call 等）                   │
  │    ├── 查找对应的处理函数（tool_handler, prompt_handler 等）                │
  │    ├── 调用处理函数                                                          │
  │    └── 返回 JSON-RPC 响应                                                    │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 5. 发送响应到 stdout                                                       │
  │                                                                                │
  │    std::cout << res.dump() << "\n" << std::flush;                           │
  │                                                                                │
  │    注意：                                                                    │
  │    • "\n" 是换行符，管道通信通常按行读取                                      │
  │    • std::flush 确保数据立即发送到管道，而不是留在缓冲区                     │
  │                                                                                │
  │    ┌─────────────────┐        stdout         ┌─────────────────┐            │
  │    │    Server       │──────────────────────►│    OpenCode    │            │
  │    │                 │      管道写入          │                 │            │
  │    │ std::cout     │──────────────────────►│ ReadFile()    │            │
  │    │  (写入响应)   │       + flush         │  读取响应      │            │
  │    └─────────────────┘                        └─────────────────┘            │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
         │
         ▼
  ┌─────────────────────────────────────────────────────────────────────────────────┐
  │ 6. 继续等待下一个请求                                                       │
  │                                                                                │
  │    while 循环回到步骤 2，继续阻塞等待                                         │
  │                                                                                │
  │    直到：                                                                    │
  │    - OpenCode 关闭管道（退出）                                               │
  │    - std::getline() 返回 false                                              │
  │    - running_ 被设为 false                                                   │
  │                                                                                │
  └───────────────────────────────────────────────────────────────────────────────┘
```

---

## 5. 完整通信流程

### 5.1 端到端时序图

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                           Stdio 管道通信完整时序图                                          │
└─────────────────────────────────────────────────────────────────────────────────────────────┘

  时间线
  │  OpenCode          操作系统              你的 Server (start_stdio)
  │   (父进程)           (管道)                   (子进程)
  │     │                 │                        │
  │     │ 1. 启动子进程   │                        │
  │     ├────────────────►│                        │
  │     │                 │  CreateProcess()       │
  │     │                 │       │                │
  │     │                 │       ▼                │
  │     │                 │  启动子进程            │
  │     │                 │       │                │
  │     │                 │       ▼                │
  │     │                 │                        ├─► main()
  │     │                 │                        ├─► register_tool()
  │     │                 │                        ├─► start_stdio()
  │     │                 │                        │     │
  │     │                 │                        │     ▼
  │     │                 │                        │  std::getline() [阻塞等待]
  │     │                 │                        │     │
  │     │                 │                        │     │
  │     │                 │                        │     │
  │  2. 发送 initialize 请求                     │
  │     │                 │                        │
  │     ├────────────────►│                        │
  │     │  JSON-RPC        │                        │
  │     │  (通过 stdin)   │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   std::getline() [返回]
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                   3. 解析 JSON
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   4. process_request()
  │     │                 │                        │
  │     │                 │                        │── 查找 on_initialize
  │     │                 │                        │── 调用 handler
  │     │                 │                        │── 返回响应
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   5. std::cout + flush
  │     │                 │                        │
  │     │                 │                        │  JSON-RPC 响应
  │     │                 │                        │  (通过 stdout)
  │     │                 │                        ▼
  │     │                 │                        │
  │     │◄────────────────┤                        │
  │     │  JSON-RPC 响应  │                        │
  │     │                 │                        │
  │     │                 │                        │
  │  6. 发送 tools/list 请求                      │
  │     │                 │                        │
  │     ├────────────────►│                        │
  │     │                 │                        ▼
  │     │                 │                   std::getline() [返回]
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                   解析 + 处理
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   std::cout + flush
  │     │                 │                        │
  │     │◄────────────────┤                        │
  │     │                 │                        │
  │     │                 │                        │
  │     │  ... 更多请求 ... │                        │
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   std::getline() [阻塞等待]
  │     │                 │                        │
  │     │                 │                        │
  │     │ 7. OpenCode 退出                         │
  │     │    管道关闭       │                        │
  │     ├────────────────►│                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   std::getline() [返回 false]
  │     │                 │                        │
  │     │                 │                        │
  │     │                 │                        ▼
  │     │                 │                   while 循环结束
  │     │                 │                   running_ = false
  │     │                 │                   Server 退出
```

### 5.2 消息格式示例

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              JSON-RPC 消息格式示例                                   │
└─────────────────────────────────────────────────────────────────────────────────────┘

  OpenCode → Server (stdin):
  ─────────────────────────────────────────────────────────────────────────────────
  {"jsonrpc":"2.0","id":1,"method":"initialize","params":{...}}

  Server → OpenCode (stdout):
  ─────────────────────────────────────────────────────────────────────────────────
  {"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2024-11-05",...}}

  OpenCode → Server (stdin):
  ─────────────────────────────────────────────────────────────────────────────────
  {"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}

  Server → OpenCode (stdout):
  ─────────────────────────────────────────────────────────────────────────────────
  {"jsonrpc":"2.0","id":2,"result":{"tools":[...]}}

  OpenCode → Server (stdin):
  ─────────────────────────────────────────────────────────────────────────────────
  {"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"echo","arguments":{...}}}
```

---

## 6. 与 HTTP 模式的对比

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                         HTTP vs Stdio 模式对比                                     │
└─────────────────────────────────────────────────────────────────────────────────────┘

  ┌────────────────────┬────────────────────────────────────────────────────────────┐
  │      对比项        │                        说明                                 │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 通信方式           │ HTTP 模式                    │ Stdio 模式                  │
  │                    │ TCP Socket (网络)            │ 管道 (Pipe)                 │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 启动方式           │ server.start(true)           │ server.start_stdio()        │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 监听地址           │ localhost:8080               │ stdin/stdout               │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 客户端发起方式     │ HTTP 请求                    │ 启动子进程 + 管道通信       │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 适用场景           │ 网页/远程调用                │ OpenCode/Claude Desktop    │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 路由处理           │ httplib 注册路由             │ 逐行解析 JSON-RPC          │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 响应返回           │ HTTP Response / SSE          │ stdout + flush              │
  ├────────────────────┼────────────────────────────────────────────────────────────┤
  │ 长连接             │ SSE 连接保持                 │ stdin/stdout 保持          │
  └────────────────────┴────────────────────────────────────────────────────────────┘
```

---

## 7. 代码位置索引

| 功能 | 文件位置 |
|------|----------|
| 客户端启动子进程 | `src/mcp_stdio_client.cpp:220-330` |
| Windows 管道创建 | `src/mcp_stdio_client.cpp:228-260` |
| STARTUPINFO 配置 | `src/mcp_stdio_client.cpp:263-271` |
| CreateProcess 调用 | `src/mcp_stdio_client.cpp:288-299` |
| 管道通信（读） | `src/mcp_stdio_client.cpp:599-680` |
| 管道通信（写） | `src/mcp_stdio_client.cpp:793-805` |
| 服务器 start_stdio | `src/mcp_server.cpp:56-124` |
| std::getline 读取 | `src/mcp_server.cpp:64` |
| stdout 响应输出 | `src/mcp_server.cpp:98,107` |
| JSON-RPC 解析 | `src/mcp_server.cpp:70-85` |
| 请求处理 | `src/mcp_server.cpp:91` |

---

## 8. 总结

1. **管道是单向的**：stdin 管道只能父进程→子进程，stdout 管道只能子进程→父进程
2. **每个管道需要两个句柄**：一个读，一个写，父进程和子进程各持一端
3. **STARTUPINFO 关键作用**：通过设置 `hStdInput/hStdStdOutput` 和 `STARTF_USESTDHANDLES` 标志，让子进程使用管道而非默认的键盘/屏幕
4. **start_stdio() 核心逻辑**：`std::getline(std::cin, line)` 阻塞等待 + `std::cout << res << std::flush` 发送响应
5. **复用框架逻辑**：服务器端不需要重新实现工具/Prompt/Resource 处理逻辑，直接调用 `process_request()` 即可