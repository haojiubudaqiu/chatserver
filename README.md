# 高性能集群聊天服务器

基于 muduo 网络库开发的 C++ 集群聊天服务器，采用 Protobuf 二进制序列化协议，通过 Kafka 消息队列实现多服务器间的消息传递，通过 Redis 缓存热点数据提升读取性能。集成 AI 智能助手（LangChain + LangGraph + GLM-5），支持智能对话、联网搜索、代发消息等高级功能。

## 项目概述

- 用户注册、登录、注销
- 好友管理（添加好友、查看好友列表）
- 群组管理（创建群组、加入群组、群组聊天）
- 私聊消息与群聊消息
- 离线消息存储与登录推送
- 多服务器集群部署，Nginx TCP 负载均衡
- **AI 智能助手**：基于 LangChain + LangGraph 的 AI Agent，支持：
  - 🗣️ 自然语言对话
  - 🔍 联网搜索（Tavily）
  - 👥 查询好友/群组/在线用户
  - 📨 **代发消息**：让 AI 帮用户给好友发送消息
- **MCP 协议支持**：8 个 MCP 工具接口，供 AI Agent 调用

---

## AI 智能助手快速上手

详细用户指南请参阅：[docs/AGENT_USER_GUIDE.md](./docs/AGENT_USER_GUIDE.md)

### 快速示例

在客户端中与 AI 智能助手（用户 ID: 10000）对话：

| 你的输入 | AI 行为 |
|----------|---------|
| `你好！` | 问候回复 |
| `帮我看看我的好友列表` | 调用 MCP 查询好友列表 |
| `帮我给好友393发一条古风表白` | 生成内容并代发消息 |
| `搜索一下最近的科技新闻` | Tavily 联网搜索 |
| `现在谁在线？` | 查询在线用户列表 |

---

## 技术架构

```
                        客户端 (Clients)
                             │
                    ┌────────▼────────┐
                    │  Nginx (TCP LB) │  ← :7000
                    └────────┬────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │ ChatServer  │   │ ChatServer  │   │ ChatServer  │
   │  :6000      │   │  :6001      │   │  :6002      │
   │ (MCP:8888)  │   │             │   │             │
   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
          │                  │                  │
          └──────────────────┼──────────────────┘
                             │
          ┌──────────────────┼──────────────────┬──────────────────┐
          │                  │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │   Redis     │   │   MySQL     │   │   Kafka     │   │ Python Agent│
   │   纯缓存     │   │ (主从)     │   │   跨服通信    │   │ :8000 Bridge│
   └─────────────┘   └─────────────┘   └─────────────┘   └──────┬──────┘
                                                                 │
                                                          ┌──────▼──────┐
                                                          │ LangGraph   │
                                                          │ LLM Agent   │
                                                          │ (GLM-5)     │
                                                          └─────────────┘
```

### 跨服务器通信机制

Redis 仅用于数据缓存。跨服务器消息传递通过 Kafka 广播实现：

```
用户 A (Server1) → 发送私聊消息给用户 B
  ├── B 在同一台服务器？ → 直接转发给 B 的连接
  └── B 不在本机？ → 发送到 Kafka topic "user_messages"
                        ↓
         所有服务器的 Kafka 消费者收到消息
                        ↓
         检查本机的 _userConnMap 是否有用户 B
         只有 B 所在的服务器会投递消息
```

---

## 快速开始

### 环境要求

- Linux (Ubuntu 20.04+)
- Docker & Docker Compose
- CMake 3.10+
- g++ 9+
- Python 3.10+

### 启动基础设施

```bash
docker compose up -d
```

### 构建

```bash
mkdir -p build && cd build
cmake .. && make -j$(nproc)
# 或使用自动构建脚本
./autobuild.sh
```

### 启动服务器

```bash
# 启动 3 台 ChatServer + AI Agent
./start_servers.sh

# 手动启动（指定端口）
SERVER_PORT=6000 KAFKA_HOST=localhost KAFKA_PORT=9093 \
  nohup ./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888 > /tmp/server0.log 2>&1 &
```

### 启动 Bridge（REST API）

```bash
cd frontend/bridge && pip install -r requirements.txt
uvicorn main:app --host 0.0.0.0 --port 8000 --reload
```

### 启动前端

```bash
cd frontend/web && npx serve dist -l 3000
```

---

## 运行测试

```bash
# 功能测试（需服务器运行中）
./test_full.sh                    # 61 个原生测试
./test_agent_e2e.sh              # 10 个 Agent E2E 测试

# 单元测试
./bin/test_db_pool
./bin/test_redis
./bin/test_models
./bin/test_kafka
```

---

## 项目结构

```
chatserver/
├── src/server/           # C++ 服务端
│   ├── main.cpp
│   ├── chatserver.cpp    # muduo 网络层
│   ├── chatservice.cpp   # 业务逻辑层
│   ├── db/               # 数据库层
│   ├── model/            # ORM 模型
│   ├── redis/            # Redis 缓存
│   ├── kafka/            # Kafka 跨服通信
│   ├── log/              # 异步日志
│   ├── mcp/              # MCP 服务器（8个工具）
│   └── proto/            # Protobuf 定义
├── agent_service/        # AI Agent 服务
│   ├── main.py           # 入口
│   ├── agent_core.py     # LangGraph + MCP 工具
│   ├── tcp_bridge.py     # TCP 通信
│   └── config.py         # 配置
├── c++_mcp/              # MCP C++ 库
├── frontend/             # 前端 + Bridge
├── docker/               # Docker 配置
└── docs/                 # 文档
    └── AGENT_USER_GUIDE.md  # AI 智能助手用户指南
```

---

## 关键环境变量

| 变量名 | 说明 | 默认值 |
|--------|------|--------|
| `SERVER_PORT` | 服务器监听端口（也是 Kafka group ID 后缀） | 必填 |
| `KAFKA_HOST` | Kafka 地址 | `localhost` |
| `KAFKA_PORT` | Kafka 端口（Docker 暴露 9093） | `9093` |
| `MODELSCOPE_API_KEY` | ModelScope API Key | — |
| `TAVILY_API_KEY` | Tavily 搜索 API Key | — |
| `MODEL_NAME` | LLM 模型名 | `ZhipuAI/GLM-5` |
| `MYSQL_HOST` | MySQL 主机 | `127.0.0.1` |
| `MYSQL_PASSWORD` | MySQL 密码 | `123456` |
| `REDIS_SENTINEL1/2/3` | Redis Sentinel 地址 | — |
