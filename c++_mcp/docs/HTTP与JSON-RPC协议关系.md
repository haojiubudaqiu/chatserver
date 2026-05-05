# HTTP 与 JSON-RPC 2.0 协议层级关系

## 概述

本项目使用 **双层协议栈**：
1. **传输层**：HTTP/1.1 (负责网络通信)
2. **应用层**：JSON-RPC 2.0 (负责业务逻辑)

```
┌─────────────────────────────────────────────────────────────┐
│                     HTTP/1.1 协议层                          │
│  (请求行、请求头、Content-Type、TCP Socket 连接)              │
├─────────────────────────────────────────────────────────────┤
│                     JSON-RPC 2.0 协议层                       │
│  (jsonrpc、method、params、id、result/error)                │
└─────────────────────────────────────────────────────────────┘
```

---

## 1. HTTP 请求体与 JSON-RPC 的关系

### 1.1 请求体 (Request Body)

**是的，JSON-RPC 请求放在 HTTP 请求体中传输。**

```
HTTP POST /message HTTP/1.1
Host: localhost:8080
Content-Type: application/json
Mcp-Session-Id: abc123

{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
        "name": "get_weather",
        "arguments": {"location": "Beijing"}
    }
}
```

### 1.2 实际代码对应

```cpp
// mcp_server.cpp:728-736 - 服务端解析 HTTP 请求体
json req_json = json::parse(req.body);  // req.body 就是上面的 JSON

// mcp_sse_client.cpp:481 - 客户端发送 JSON-RPC 请求
http_client_->Post(msg_endpoint_, headers, req_body, "application/json");
// req_body 就是 JSON-RPC 格式的字符串
```

---

## 2. HTTP 响应体与 JSON-RPC 的关系

### 2.1 两种响应模式

本项目有两种 JSON-RPC 响应方式：

| 模式 | HTTP 响应方式 | 用途 |
|------|--------------|------|
| **SSE 推送** | 通过 `event_dispatcher` 推送 | 有 ID 的请求 |
| **直接返回** | HTTP 响应体直接返回 | 初始化请求、错误响应 |

### 2.2 成功响应 (通过 SSE)

```
HTTP/1.1 202 Accepted

SSE Stream:
event: message
data: {"jsonrpc":"2.0","id":1,"result":{...}}
```

### 2.3 初始化响应 (直接 HTTP 返回)

```cpp
// mcp_server.cpp:943-944
res.set_header("Content-Type", "application/json");
res.set_content(result.dump(), "application/json");
```

```
HTTP/1.1 200 OK
Content-Type: application/json

{
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "protocolVersion": "2025-03-26",
        "capabilities": {...},
        "serverInfo": {...}
    }
}
```

### 2.4 错误响应 (直接 HTTP 返回)

```cpp
// mcp_server.cpp:862-863
res.set_content("{\"error\":\"Session already initialized...\"}", "application/json");
```

```
HTTP/1.1 400 Bad Request
Content-Type: application/json

{
    "jsonrpc": "2.0",
    "error": {
        "code": -32600,
        "message": "Invalid Request"
    }
}
```

---

## 3. 完整请求-响应流程

### 3.1 流程图

```
客户端                                          服务端
  │                                               │
  │  1. 建立 SSE 连接                             │
  │  ─────────────────────────────────────────►   │
  │  GET /sse HTTP/1.1                            │
  │                                               │
  │  ◄─────────────────────────────────────────   │
  │  HTTP/1.1 200 OK                              │
  │  Content-Type: text/event-stream              │
  │  Mcp-Session-Id: abc123                      │
  │                                               │
  │  ◄──────────── SSE Stream ─────────────────   │
  │  event: endpoint                              │
  │  data: /message?session_id=abc123             │
  │                                               │
  │  2. 初始化请求 (通过 HTTP POST)               │
  │  ─────────────────────────────────────────►   │
  │  POST /message HTTP/1.1                       │
  │  Content-Type: application/json               │
  │  Mcp-Session-Id: abc123                      │
  │  Body: {                                      │
  │    "jsonrpc": "2.0",                         │
  │    "id": 1,                                   │
  │    "method": "initialize",                    │
  │    "params": {...}                           │
  │  }                                            │
  │                                               │
  │  ◄─────────────────────────────────────────   │
  │  HTTP/1.1 200 OK                              │
  │  Content-Type: application/json               │
  │  Body: {                                      │
  │    "jsonrpc": "2.0",                         │
  │    "id": 1,                                   │
  │    "result": {...}                           │
  │  }                                            │
  │                                               │
  │  3. 调用工具 (通过 SSE 接收响应)               │
  │  ─────────────────────────────────────────►   │
  │  POST /message HTTP/1.1                       │
  │  Content-Type: application/json               │
  │  Body: {                                      │
  │    "jsonrpc": "2.0",                         │
  │    "id": 2,                                   │
  │    "method": "tools/call",                   │
  │    "params": {...}                           │
  │  }                                            │
  │                                               │
  │  ◄──────────── SSE Stream ─────────────────   │
  │  event: message                               │
  │  data: {"jsonrpc":"2.0","id":2,"result":{...}}│
```

### 3.2 代码对照

```cpp
// 客户端发送 JSON-RPC 请求
// mcp_sse_client.cpp:481
std::string req_body = mcp_req.dump();  // JSON-RPC 格式
auto result = http_client_->Post(msg_endpoint_, headers, req_body, "application/json");

// 服务端解析请求体中的 JSON-RPC
// mcp_server.cpp:730
json req_json = json::parse(req.body);  // req.body 是 JSON-RPC

// 服务端返回 JSON-RPC 响应
// mcp_server.cpp:943-944
res.set_content(result.dump(), "application/json");  // result.dump() 是 JSON-RPC
```

---

## 4. HTTP 与 JSON-RPC 协议对比

### 4.1 HTTP 层面

| 字段 | 作用 |
|------|------|
| `POST /message` | 端点路径 |
| `Content-Type: application/json` | 告诉对方请求体是 JSON |
| `Mcp-Session-Id` | 关联 HTTP 请求和 SSE 连接 |
| `200/202/400/404` | HTTP 状态码 |

### 4.2 JSON-RPC 2.0 层面

| 字段 | 作用 |
|------|------|
| `jsonrpc: "2.0"` | JSON-RPC 协议版本 |
| `id` | 请求标识 (用于配对响应) |
| `method` | 要调用的方法名 |
| `params` | 方法参数 |
| `result` | 成功响应数据 |
| `error` | 错误响应数据 |

### 4.3 请求格式

```json
// JSON-RPC Request
{
    "jsonrpc": "2.0",
    "id": 1,
    "method": "tools/call",
    "params": {
        "name": "get_weather",
        "arguments": {"location": "Beijing"}
    }
}
```

### 4.4 成功响应格式

```json
// JSON-RPC Success Response
{
    "jsonrpc": "2.0",
    "id": 1,
    "result": {
        "content": [
            {"type": "text", "text": "Weather: Sunny, 25°C"}
        ],
        "isError": false
    }
}
```

### 4.5 错误响应格式

```json
// JSON-RPC Error Response
{
    "jsonrpc": "2.0",
    "id": 1,
    "error": {
        "code": -32602,
        "message": "Invalid params",
        "data": {"details": "Missing required parameter: location"}
    }
}
```

---

## 5. 为什么这样设计？

### 5.1 分层优点

```
┌────────────────────────────────────────────────────────┐
│  为什么用 HTTP 作为传输层？                              │
├────────────────────────────────────────────────────────┤
│  ✅ 广泛支持：所有语言都有 HTTP 库                        │
│  ✅ 防火墙友好：使用标准 80/443 端口                     │
│  ✅ 成熟稳定：HTTP/1.1 经过 decades 验证                │
│  ✅ 工具丰富：curl、postman、浏览器都能测试              │
└────────────────────────────────────────────────────────┘

┌────────────────────────────────────────────────────────┐
│  为什么用 JSON-RPC 2.0 作为应用层？                     │
├────────────────────────────────────────────────────────┤
│  ✅ 简单明了：只有 few 种消息类型                        │
│  ✅ 语言无关：JSON 格式所有语言都能解析                  │
│  ✅ 标准化：JSON-RPC 2.0 是开放标准                      │
│  ✅ 有序扩展：错误码、方法名 可自定义                    │
└────────────────────────────────────────────────────────┘
```

### 5.2 与 RESTful 对比

| 特性 | RESTful | JSON-RPC 2.0 |
|------|---------|--------------|
| 语义 | 资源导向 (GET/POST/PUT/DELETE) | 方法导向 (method name) |
| 灵活性 | 路径 + 动词组合 | 统一的 {method, params} |
| 适用场景 | CRUD 操作 | 远程过程调用 |
| 本项目场景 | ❌ 不适用 | ✅ 适用 (MCP 协议) |

---

## 6. 实际例子

### 6.1 initialize 请求

**HTTP 请求：**
```
POST /message HTTP/1.1
Content-Type: application/json
Mcp-Session-Id: abc123
Content-Length: 156

{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"TestClient","version":"1.0.0"}}}
```

**HTTP 响应：**
```
HTTP/1.1 200 OK
Content-Type: application/json

{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-03-26","capabilities":{"tools":{}},"serverInfo":{"name":"MCP Server","version":"0.0.1"}}}
```

### 6.2 tools/call 请求

**HTTP 请求：**
```
POST /message HTTP/1.1
Content-Type: application/json
Mcp-Session-Id: abc123

{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"get_weather","arguments":{"location":"Beijing"}}}
```

**SSE 响应：**
```
event: message
data: {"jsonrpc":"2.0","id":2,"result":{"content":[{"type":"text","text":"Weather in Beijing: Sunny, 25°C"}],"isError":false}}
```

---

## 7. 总结

### HTTP 与 JSON-RPC 的关系

```
                    ┌─────────────────────────────┐
                    │        HTTP 请求体           │
                    │                             │
                    │  Content-Type: application/json
                    │                             │
                    │  ┌───────────────────────┐  │
                    │  │   JSON-RPC 2.0        │  │
                    │  │                       │  │
                    │  │  {"jsonrpc": "2.0",   │  │
                    │  │   "method": "...",    │  │
                    │  │   "params": {...},    │  │
                    │  │   "id": 1}            │  │
                    │  │                       │  │
                    │  └───────────────────────┘  │
                    └─────────────────────────────┘

                    ┌─────────────────────────────┐
                    │        HTTP 响应体          │
                    │                             │
                    │  Content-Type: application/json
                    │                             │
                    │  ┌───────────────────────┐  │
                    │  │   JSON-RPC 2.0        │  │
                    │  │                       │  │
                    │  │  {"jsonrpc": "2.0",   │  │
                    │  │   "id": 1,            │  │
                    │  │   "result": {...}}    │  │
                    │  │                       │  │
                    │  └───────────────────────┘  │
                    └─────────────────────────────┘
```

**结论：JSON-RPC 2.0 消息是封装在 HTTP 请求体/响应体中的。**