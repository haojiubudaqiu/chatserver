# ChatPulse — 脉聊集群智能聊天服务器

[![Ubuntu](https://img.shields.io/badge/Platform-Ubuntu%2020.04%2B-orange)](https://ubuntu.com)
[![C++](https://img.shields.io/badge/C%2B%2B-17-blue)](https://isocpp.org)
[![Python](https://img.shields.io/badge/Python-3.10%2B-green)](https://python.org)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)

**ChatPulse** 是一个基于 **muduo** 网络库开发的高性能集群聊天服务器，采用 **Protobuf** 二进制序列化协议，通过 **Kafka** 消息队列实现多服务器间的实时消息传递，**Redis** 缓存热点数据提升读取性能，**MySQL** 主从架构配合自动读写分离实现数据持久化。

集成 **AI 智能助手**（LangChain + LangGraph + ModelScope LLM），支持自然语言对话、联网搜索、代发消息、查询好友/群组/在线用户等功能。

提供 **Web 前端**（React + TypeScript）和 **Python Bridge**（FastAPI + WebSocket），浏览器即可完成注册、聊天、AI 互动等全部操作。

---

## 目录

- [功能特性](#功能特性)
- [技术架构](#技术架构)
- [快速开始](#快速开始)
- [AI 智能助手](#ai-智能助手)
- [项目结构](#项目结构)
- [运行测试](#运行测试)
- [关键环境变量](#关键环境变量)

---

## 功能特性

### 聊天功能
- 用户注册、登录、注销、状态管理
- 一对一私聊、群组聊天
- 好友管理（添加好友、查看好友列表、在线状态）
- 群组管理（创建群组、加入群组）
- 离线消息存储与登录推送
- 聊天历史记录分页查询

### 集群架构
- 多服务器实例水平扩展
- Nginx TCP 四层负载均衡
- **Kafka** 跨服务器消息广播（非 Redis Pub/Sub）
- **MySQL** 主从架构 + **DatabaseRouter** 自动读写分离
- **Redis** 主从 + **Sentinel** 高可用缓存

### AI 智能助手
- 🗣️ **自然语言对话**：基于 LangChain + LangGraph + ModelScope LLM（MiniMax-M1-80k / GLM-5 / DeepSeek-R1）
- 🔍 **联网搜索**：Tavily 实时搜索，支持天气、新闻、股票等查询
- 👥 **查询信息**：查好友列表、群组信息、在线用户、服务器统计
- 📨 **代发消息**：让 AI 帮用户给好友发送消息
- 🔄 **智能容错**：API 限流时自动切换可用模型
- 🧠 **思考过程透明**：自动剥离 `<think>` 推理标签，只展示最终回答

### MCP 协议支持
内置 **8 个 MCP 工具接口**（Streamable HTTP，2025-03-26 规范），供 AI Agent 调用：

| 工具 | 功能 |
|------|------|
| `chat_user_login` | 验证用户凭据，返回用户信息 |
| `chat_send_message` | 代发私聊消息 |
| `chat_server_stats` | 服务器统计信息 |
| `chat_list_online_users` | 在线用户列表 |
| `chat_get_user_info` | 查询用户信息 |
| `chat_get_user_friends` | 查询好友列表 |
| `chat_get_group_info` | 查询群组详情 |
| `chat_list_user_groups` | 用户所属群组列表 |

---

## 技术架构

```
┌──────────────────────────────────────────────────────────────────┐
│                    Windows 浏览器 (React SPA)                     │
│               http://localhost:3000 (或 Vite Dev :5173)           │
└──────────────────────────┬───────────────────────────────────────┘
                           │ HTTP / WebSocket
┌──────────────────────────▼───────────────────────────────────────┐
│               Python Bridge (FastAPI :8000)                       │
│     REST: 注册/登录/发消息/查历史    WS: 实时消息推送            │
└──────────────────────────┬───────────────────────────────────────┘
                           │ TCP / Protobuf
           ┌───────────────┼───────────────────┐
           │               │                   │
┌──────────▼────────┐ ┌───▼──────────┐ ┌──────▼──────────┐
│  ChatPulse 实例 1  │ │ 实例 2      │ │ 实例 3          │
│  TCP :6000        │ │ TCP :6001   │ │ TCP :6002       │
│  MCP :8888        │ │             │ │                 │
└──────────┬────────┘ └──────┬───────┘ └────────┬────────┘
           │                 │                   │
           └─────────────────┼───────────────────┘
                             │
       ┌─────────────────────┼─────────────────────┬──────────────────┐
       │                     │                     │                  │
┌──────▼──────┐      ┌──────▼──────┐       ┌──────▼──────┐   ┌──────▼──────┐
│  Redis      │      │  MySQL      │       │  Kafka      │   │  AI Agent   │
│  缓存       │      │  主从读写分离│       │  跨服消息    │   │  (Python)   │
│             │      │             │       │             │   │             │
│ INFO TTL:   │      │ user        │       │ user_messages│  │ LangGraph   │
│   30min     │      │ friend      │       │ group_msg    │  │ ModelScope  │
│ FRIEND TTL: │      │ allgroup    │       │             │   │ Tavily 搜索 │
│   15min     │      │ groupuser   │       │             │   │ MCP Tools   │
│ STATUS TTL: │      │ chat_message│       │             │   │             │
│   5min      │      │ offlinemsg  │       │             │   │             │
└─────────────┘      └─────────────┘       └─────────────┘   └─────────────┘
```

### 跨服务器通信机制

Redis 仅用于数据缓存。跨服务器消息传递通过 Kafka 广播实现：

```
用户 A 在 Server1 发送私聊给用户 B
  ├── B 在 Server1 上？ → 直接查 _userConnMap 转发
  └── B 不在本机？     → 发布到 Kafka topic "user_messages"
                         ↓
              所有 Server 的 Kafka 消费者收到消息
                         ↓
              各 Server 检查本机 _userConnMap
              只有 B 所在的那台 Server 会投递
```

### 数据流：Web 消息发送

```
浏览器 → POST /api/send_message → Bridge → TCP/Protobuf → ChatServer
                                                             │
                                                   写入 MySQL chat_message 表
                                                             │
                                            接收方在同一 Server？→ 直接推送
                                            接收方在不同 Server？→ Kafka 广播
                                                             │
                                                    Bridge 收到响应
                                                             │
                                                WebSocket 推送到接收方浏览器
```

### 数据流：AI Agent 回复

```
用户发送消息给 AI (ID=10000) → ChatServer → TCP → Agent
                                                     │
                                              Agent 处理 (LLM 推理)
                                                     │
                                       需要查信息？→ MCP HTTP 调用服务器工具
                                       需要搜索？  → Tavily API
                                                     │
                                             合成回复 → TCP → ChatServer
                                                             │
                                                    Bridge → WebSocket → 浏览器
```

---

## 快速开始

### 方式一：Docker 一键部署（推荐）

只需 Docker，无需安装任何 C++/Python/Node.js 工具链：

```bash
# 1. 安装 Docker
sudo apt install -y docker.io docker-compose-v2
sudo usermod -aG docker $USER && newgrp docker

# 2. 克隆并启动
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver

# 3.（可选）配置 AI API Key
cp .env.example .env
# 编辑 .env 填入 MODELSCOPE_API_KEY

# 4. 一键启动（首次构建约 15 分钟，包含 protobuf + muduo 编译）
docker compose up -d

# 5. 验证
docker compose ps                     # 所有 17 个容器应为 Up
curl http://127.0.0.1:8080/health     # Nginx: OK
curl http://127.0.0.1:8000/api/me/9999  # Bridge: {"detail":"Not logged in"}

# 6. 打开浏览器访问 http://localhost:3000
```

### 方式二：本地开发（直接编译运行）

> 详细从零搭建步骤请参阅 **[docs/QUICK_START.md](docs/QUICK_START.md)**

```bash
# 1. 安装依赖
sudo apt install -y docker.io docker-compose-v2 cmake g++ python3-pip nodejs
pip3 install -r frontend/bridge/requirements.txt
pip3 install -r agent_service/requirements.txt
cd frontend/web && npm install && cd ../..

# 2. 启动基础设施（MySQL, Redis, Kafka, Nginx）
docker compose up -d

# 3. 编译 C++ 服务端
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..

# 4. 重置 AI 用户状态
docker exec chat_mysql_master mysql -h127.0.0.1 -uroot -p'Sf523416&111' chat -e \
  "UPDATE user SET state='offline' WHERE id=10000;"

# 5. 启动 3 个 Server 实例
export KAFKA_HOST=localhost KAFKA_PORT=9093
SERVER_PORT=6000 setsid nohup ./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888 > /tmp/s0.log &
SERVER_PORT=6001 setsid nohup ./bin/ChatServer 0.0.0.0 6001 > /tmp/s1.log &
SERVER_PORT=6002 setsid nohup ./bin/ChatServer 0.0.0.0 6002 > /tmp/s2.log &
sleep 3

# 6. 启动 Bridge + AI Agent
cd frontend/bridge && setsid nohup uvicorn main:app --host 0.0.0.0 --port 8000 > /tmp/bridge.log &
cd ../..
export MODELSCOPE_API_KEY="ms-你的KEY"
setsid nohup python3 -u agent_service/main.py > /tmp/agent.log &

# 7. Windows 前端
# cd frontend/web
# set VITE_BRIDGE_URL=http://192.168.1.100:8000
# npm run dev     # 开发模式
# npx serve dist -l 3000  # 生产模式
```

---

## AI 智能助手

详细用户指南请参阅：

> **[docs/AGENT_USER_GUIDE.md](docs/AGENT_USER_GUIDE.md)**

### 快速示例

注册并登录后，在聊天中点击"AI智能助手"，输入以下内容体验 AI 能力：

| 你的输入 | AI 行为 |
|----------|---------|
| `你好！你是谁？` | AI 自我介绍 |
| `帮我看看我的好友列表` | 调用 MCP 查询好友 |
| `搜索一下最近的科技新闻` | Tavily 联网搜索，返回实时资讯 |
| `今天天气怎么样？` | Tavily 搜索当前天气（含日期上下文） |
| `帮我给好友392发一条消息：明天一起吃饭` | 生成内容并代发给好友 392 |
| `现在谁在线？` | 查询在线用户列表 |
| `查看服务器统计` | 查询服务器统计信息 |

### 配置说明

AI Agent 依赖以下环境变量：

| 变量 | 说明 | 获取方式 |
|------|------|----------|
| `MODELSCOPE_API_KEY` | **必填** ModelScope API Key | [modelscope.cn](https://www.modelscope.cn) 注册获取 |
| `TAVILY_API_KEY` | 可选，联网搜索 | [tavily.com](https://tavily.com) 注册获取 |

默认模型为 `MiniMax/MiniMax-M1-80k`，限流时自动切换至 `ZhipuAI/GLM-5` → `deepseek-ai/DeepSeek-R1-0528`。

---

## 项目结构

```
chatserver/
├── src/server/                    # C++ 服务端
│   ├── main.cpp                   # 入口，注册消息处理器
│   ├── chatserver.cpp             # muduo TcpServer 网络层
│   ├── chatservice.cpp            # 业务逻辑层（单例）
│   ├── db/                        # MySQL 连接池 + DatabaseRouter
│   │   ├── db.cpp
│   │   ├── connection_pool.cpp
│   │   └── database_router.cpp
│   ├── model/                     # ORM 数据模型
│   │   ├── usermodel.cpp
│   │   ├── friendmodel.cpp
│   │   ├── groupmodel.cpp
│   │   ├── offlinemessagemodel.cpp
│   │   └── chathistorymodel.cpp
│   ├── redis/                     # Redis 缓存（仅缓存，非 Pub/Sub）
│   │   ├── redis_cache.cpp
│   │   ├── cache_manager.cpp
│   │   └── redis_sentinel.cpp
│   ├── kafka/                     # Kafka 跨服通信
│   │   ├── kafka_manager.cpp
│   │   ├── kafka_producer.cpp
│   │   └── kafka_consumer.cpp
│   ├── log/                       # 异步日志（muduo 风格双缓冲）
│   │   ├── async_logging.cpp
│   │   └── log_file.cpp
│   ├── mcp/                       # MCP Server（8 个工具接口）
│   │   ├── chat_mcp_server.cpp
│   │   └── chat_mcp_server.h
│   ├── proto/                     # Protobuf 消息定义
│   │   └── message.proto
│   └── peer_relay.cpp             # Kafka 跨服消息处理
├── agent_service/                 # AI Agent 服务（Python）
│   ├── main.py                    # 入口点
│   ├── agent_core.py              # LangGraph + LLM + MCP 工具
│   ├── tcp_bridge.py              # TCP 客户端（连接 ChatServer）
│   ├── config.py                  # 环境变量配置
│   └── requirements.txt           # Python 依赖
├── c++_mcp/                       # MCP C++ 库（HTTP + SSE + Stdio）
├── frontend/
│   ├── bridge/                    # Python Bridge（FastAPI）
│   │   ├── main.py                # REST + WebSocket 接口
│   │   ├── chat_protocol.py       # Protobuf TCP 通信层
│   │   └── requirements.txt
│   └── web/                       # React 前端（TypeScript + Vite）
│       ├── src/
│       │   ├── App.tsx            # 主界面（登录/注册/聊天/好友/群组）
│       │   └── App.css            # Markdown 渲染样式
│       └── package.json
├── docker/                        # Docker 配置文件
│   ├── mysql/
│   │   ├── init.sql               # 数据库初始化 + 预置数据
│   │   ├── master.cnf
│   │   └── slave1.cnf / slave2.cnf
│   └── ...
├── test/                          # 测试代码
│   ├── test_db_pool.cpp
│   ├── test_redis.cpp
│   ├── test_models.cpp
│   ├── test_kafka.cpp
│   └── test_e2e.py
├── docs/                          # 文档
│   ├── QUICK_START.md             # 从零搭建指南
│   └── AGENT_USER_GUIDE.md        # AI 智能助手用户指南
├── docker-compose.yml             # 17 个容器一键编排
├── Dockerfile.server              # ChatServer 多阶段构建
├── Dockerfile.web                 # Web 前端多阶段构建（Node → Nginx）
├── Dockerfile.nginx               # Nginx 负载均衡器
├── nginx.conf                     # Nginx TCP 负载均衡配置
├── nginx-web.conf                 # Nginx Web 前端 + API 代理配置
├── start_servers.sh               # 本地开发启动脚本
├── test_full.sh                   # 61 项功能测试
└── test_agent_e2e.sh              # 10 项 Agent E2E 测试
```

---

## 运行测试

```bash
# 功能测试（需要服务器正在运行）
./test_full.sh                    # 61 个原生测试
./test_agent_e2e.sh               # 10 个 Agent E2E 测试

# 单元测试
./bin/test_db_pool
./bin/test_redis
./bin/test_models
./bin/test_kafka
```

---

## 关键环境变量

| 变量名 | 说明 | 默认值 |
|--------|------|--------|
| `SERVER_PORT` | **必填** 服务器监听端口（也是 Kafka group ID 后缀） | — |
| `KAFKA_HOST` | Kafka 地址 | `localhost` |
| `KAFKA_PORT` | Kafka 端口（Docker 外部监听 9093） | `9093` |
| `MODELSCOPE_API_KEY` | ModelScope API Key（AI Agent 必须） | — |
| `TAVILY_API_KEY` | Tavily 联网搜索 API Key（可选） | — |
| `MODEL_NAME` | LLM 模型名 | `MiniMax/MiniMax-M1-80k` |
| `AI_USER_ID` | AI Agent 用户 ID | `10000` |
| `AI_PASSWORD` | AI Agent 登录密码 | `ai_token_123` |
| `MCP_SERVER_URL` | MCP 服务端点 | `http://127.0.0.1:8888/mcp` |
| `MYSQL_HOST` | MySQL 主库地址 | `127.0.0.1` |
| `MYSQL_USER` | MySQL 用户名 | `chatuser` |
| `MYSQL_PASSWORD` | MySQL 密码 | `chatpass123` |
| `MYSQL_DATABASE` | MySQL 数据库名 | `chat` |
| `MYSQL_SLAVES` | MySQL 从库列表（逗号分隔 host:port） | — |
| `REDIS_HOST` | Redis 地址 | `127.0.0.1` |
| `REDIS_PORT` | Redis 端口 | `6379` |
| `REDIS_SENTINEL1/2/3` | Redis Sentinel 地址（高可用模式） | — |
| `VITE_BRIDGE_URL` | 前端 Bridge 地址（Windows 上设置） | `http://127.0.0.1:8000` |

---

## 设计要点

- **Protobuf** 是唯一序列化格式，JSON 已完全移除
- **Kafka** 是跨服务器消息传递的唯一方式，Redis **不用于** Pub/Sub
- **SERVER_PORT** 必须为每个实例设置唯一值，用作 Kafka 消费者组 ID 后缀，确保广播语义
- **DatabaseRouter** 自动将读请求路由到从库，写请求到主库。业务层可传 `forceMaster=true` 强制走主库读
- **Redis TTL**：用户信息 30 分钟、好友列表 15 分钟、群组信息 10 分钟、用户在线状态 5 分钟
- AI Agent 通过 **MCP 官方 Python SDK**（`mcp>=1.3.0` + `langchain-mcp-adapters`）调用服务器工具
- 前端通过 **WebSocket** 实时接收消息推送，`useMemo` + `sort` 保证显示顺序正确
- **模型自动切换**：API 限流或异常时自动轮换到下一个可用模型

---

## 一键 Docker 部署（推荐）

```bash
# 1. 克隆项目
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver

# 2. 配置 API Key（可选，AI 需要）
cp .env.example .env
# 编辑 .env 填入真实的 API Key

# 3. 一键启动全部 17 个容器
docker compose up -d

# 等待约 2 分钟后，验证所有服务运行正常
docker compose ps

# 4. 打开浏览器访问 Web 前端
# http://localhost:3000
```

就这么简单。`docker compose up -d` 会自动构建并启动全部 17 个容器：

| 层 | 容器 | 数量 |
|----|------|------|
| 基础设施 | MySQL 主从、Redis 主从+Sentinel、ZooKeeper、Kafka | 10 |
| 服务层 | 3×ChatServer + Nginx LB | 4 |
| AI 层 | Bridge (FastAPI) + Agent (LangChain) + Web 前端 | 3 |

AI Agent 在未配置 `MODELSCOPE_API_KEY` 时会自动以开发模式运行（使用 Dummy LLM），可以测试对话流程但不会产生有意义的回复。

### 逐层启动（调试用）

```bash
# 仅基础设施
docker compose up -d mysql-master redis kafka

# 基础设施 + 服务层（无 AI）
docker compose up -d mysql-master mysql-slave1 mysql-slave2 redis redis-slave1 redis-slave2 \
  redis-sentinel1 redis-sentinel2 redis-sentinel3 zookeeper kafka \
  chat_server_1 chat_server_2 chat_server_3 nginx

# 全部（含 AI Agent + Bridge + Web 前端）
docker compose up -d
```

## Docker 端口映射

| 容器 | 内部端口 | 外部端口 | 说明 |
|------|----------|----------|------|
| MySQL Master | 3306 | 3306 | 主库 |
| MySQL Slave1 | 3306 | 3307 | 从库 |
| MySQL Slave2 | 3306 | 3308 | 从库 |
| Redis | 6379 | 6379 | 缓存主节点 |
| Redis Slave1 | 6379 | 6380 | 缓存从节点 |
| Redis Slave2 | 6379 | 6381 | 缓存从节点 |
| Sentinel 1-3 | 26379 | 26379-26381 | 哨兵 |
| Kafka INTERNAL | 9092 | 9092 | 容器间通信 |
| Kafka EXTERNAL | 9093 | 9093 | 宿主机访问 |
| ChatPulse Server1 | 6000/8888 | 6000/8888 | TCP + MCP |
| ChatPulse Server2 | 6001 | 6001 | TCP |
| ChatPulse Server3 | 6002 | 6002 | TCP |
| Nginx TCP LB | 7000 | 7000 | 负载均衡 |
| Nginx HTTP | 8080 | 8080 | 健康检查 |
| **Bridge** | **8000** | **8000** | REST + WebSocket |
| **Web 前端** | **80** | **3000** | React SPA |

---

## License

MIT
