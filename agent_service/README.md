# AI 智能助手服务

基于 LangChain + LangGraph 的 AI 聊天机器人，作为 ChatServer 集群的一个特殊用户（ID: 10000）运行。

## 架构

```
用户 (ChatClient/Web) ──TCP(Protobuf)──▶ ChatServer:6000
                                               │
                                          ONE_CHAT_MSG
                                          (发给 AI:10000)
                                               │
                                               ▼
                              ┌──────────────────────────────────┐
                              │       Python Agent Service        │
                              │                                    │
                              │  AgentTcpClient ──▶ ChatAgent     │
                              │  (TCP 监听/收发)     (LangGraph)   │
                              │                        │          │
                              │                        ▼          │
                              │  McpSession ──▶ ChatServer:8888   │
                              │  (HTTP MCP)      (内置工具)        │
                              │                                    │
                              │  Tavily ──▶ Web Search            │
                              └──────────────────────────────────┘
```

- **TCP 通道**：Agent 通过原生 TCP 协议连接 ChatServer，作为用户 10000 登录
- **MCP 通道**：通过 HTTP 调用 ChatServer 内置的 MCP 工具（查好友、查群组、发消息等）
- **LangGraph 工作流**：LLM 推理 → 工具调用 → 回复生成

## 前置条件

- Python 3.12+
- 运行中的 ChatServer 集群（含 MCP Server 端口 8888）
- ModelScope API Key（用于 LLM）
- Tavily API Key（可选，用于联网搜索）

## 环境变量

| 变量 | 默认值 | 说明 |
|---|---|---|
| `AI_USER_ID` | `10000` | AI 助手的用户 ID |
| `AI_PASSWORD` | `ai_token_123` | AI 助手的登录密码 |
| `CHAT_SERVER_HOST` | `127.0.0.1` | ChatServer 主机 |
| `CHAT_SERVER_PORT` | `6000` | ChatServer TCP 端口 |
| `MCP_SERVER_URL` | `http://127.0.0.1:8888/mcp` | MCP HTTP 端点 |
| `MODELSCOPE_API_KEY` | `(空)` | ModelScope API Key |
| `MODELSCOPE_BASE_URL` | `https://api-inference.modelscope.cn/v1` | ModelScope 地址 |
| `MODEL_NAME` | `qwen-max` | LLM 模型名称 |
| `TAVILY_API_KEY` | `(空)` | Tavily Web Search API Key |
| `LOG_LEVEL` | `INFO` | 日志级别 |
| `RECONNECT_DELAY` | `5` | 断线重连延迟（秒） |

## 本地启动

```bash
cd agent_service

# 安装依赖
pip install -r requirements.txt

# 设置环境变量
export MODELSCOPE_API_KEY="ms-xxxxx"
export TAVILY_API_KEY="tvly-xxxxx"

# 启动
python main.py
```

## Docker 部署

```bash
cd agent_service
docker build -t chat-ai-agent .

docker run -d \
  --name chat_ai_agent \
  --network chat_network \
  -e CHAT_SERVER_HOST=chat_server_1 \
  -e CHAT_SERVER_PORT=6000 \
  -e MCP_SERVER_URL=http://chat_server_1:8888/mcp \
  -e MODELSCOPE_API_KEY="ms-xxxxx" \
  -e TAVILY_API_KEY="tvly-xxxxx" \
  chat-ai-agent
```

或使用 `docker-compose.yml`（已包含 `chat_agent` 服务）。

## 测试

```bash
cd agent_service
pip install pytest pytest-asyncio aiohttp
python -m pytest tests/ -v
```

### 测试内容

| 文件 | 测试内容 |
|---|---|
| `tests/test_protocol.py` | TCP 帧编解码、Protobuf 序列化、离线消息 base64 |
| `tests/test_mcp_client.py` | MCP session 初始化、工具列表、工具调用 |
| `tests/test_agent_core.py` | Agent 初始化、消息处理、记忆隔离 |
| `tests/test_tcp_bridge.py` | 发送帧格式、自循环过滤 |

## 功能列表

1. **日常闲聊** — 基于 ModelScope Qwen-Max 的自然对话
2. **联网搜索** — 通过 Tavily 搜索最新资讯
3. **查好友列表** — 调用 MCP `chat_get_user_friends`
4. **查用户信息** — 调用 MCP `chat_get_user_info`
5. **查群组信息** — 调用 MCP `chat_get_group_info`
6. **查用户所在群组** — 调用 MCP `chat_list_user_groups`
7. **查在线用户** — 调用 MCP `chat_list_online_users`
8. **查服务器统计** — 调用 MCP `chat_server_stats`
9. **代发消息** — 调用 MCP `chat_send_message` 帮用户给好友发消息

## 注意事项

- **AI 用户 ID 固定为 10000** — 所有新用户注册时自动添加 AI 为好友
- **防自循环** — Agent 不会处理自己发出的消息（`fromid == 10000` 跳过）
- **MCP 每次调用新建 session** — C++ MCP 服务器不保证 session 持久化
- **断线重连** — TCP 断开后自动重连（默认 5 秒间隔）
- **离线消息** — Agent 登录后会自动处理积压的离线消息
