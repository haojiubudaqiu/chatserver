# ChatServer 集群聊天服务器 — 从零启动指南

本文档帮助一个小白用户从 git clone 开始，在 **Ubuntu 服务器** 上搭建完整后端，在 **Windows 电脑** 上运行前端，最终在浏览器中完成注册、聊天、AI 智能助手等一系列功能。

---

## 目录

1. [整体架构概览](#1-整体架构概览)
2. [Ubuntu 服务器：安装基础环境](#2-ubuntu-服务器安装基础环境)
3. [Ubuntu 服务器：克隆代码并构建](#3-ubuntu-服务器克隆代码并构建)
4. [Ubuntu 服务器：启动基础设施（Docker）](#4-ubuntu-服务器启动基础设施docker)
5. [Ubuntu 服务器：启动服务器进程](#5-ubuntu-服务器启动服务器进程)
6. [Ubuntu 服务器：启动 AI 智能助手（可选）](#6-ubuntu-服务器启动-ai-智能助手可选)
7. [Windows 电脑：启动前端](#7-windows-电脑启动前端)
8. [验证所有服务正常](#8-验证所有服务正常)
9. [常见问题排查](#9-常见问题排查)
10. [服务端口速查表](#10-服务端口速查表)

---

## 1. 整体架构概览

```
Windows 浏览器 (React SPA)
        │
        │ HTTP / WebSocket
        ▼
Ubuntu ── Python Bridge (FastAPI :8000)
        │
        │ TCP / Protobuf
        ├── ChatServer 1 (:6000) ←─ 带有 MCP AI 接口 (:8888)
        ├── ChatServer 2 (:6001)
        ├── ChatServer 3 (:6002)
        │
        ├── MySQL 主从 (3306/3307/3308) — 持久化存储
        ├── Redis 主从 + Sentinel (6379/6380/6381) — 缓存
        ├── Kafka (:9093) — 跨服务器消息传递
        └── AI Agent 进程 — 智能对话 + 联网搜索 + 工具调用
```

**通信方式**：
- 浏览器 → Bridge：HTTP REST（发送消息、查好友等）+ WebSocket（实时推送）
- Bridge → ChatServer：TCP + Protobuf（二进制协议）
- ChatServer 之间：Kafka 广播消息
- AI Agent → MCP：HTTP 调用服务器工具（查好友、发消息等）

---

## 2. Ubuntu 服务器：安装基础环境

> 推荐 Ubuntu 22.04/24.04，至少 4GB 内存、20GB 磁盘。

### 2.1 安装 Docker

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y docker.io docker-compose-v2
sudo systemctl enable docker
sudo systemctl start docker

# 将当前用户加入 docker 组（避免每次 sudo）
sudo usermod -aG docker $USER

# ⚠️ 重要：执行完上面命令后，请退出终端重新登录（exit 关掉重开）
# 验证 Docker 安装成功
docker ps
```

### 2.2 安装 C++ 编译工具链

```bash
sudo apt install -y build-essential cmake git
```

### 2.3 安装 C++ 依赖库

```bash
# MySQL 客户端库（编译 ChatServer 需要）
sudo apt install -y libmysqlclient-dev

# Redis 客户端库
sudo apt install -y libhiredis-dev

# Boost 库（muduo 依赖）
sudo apt install -y libboost-all-dev

# OpenSSL
sudo apt install -y libssl-dev

# Kafka 客户端库（可选，编译时自动检测）
sudo apt install -y librdkafka-dev
```

### 2.4 安装 Python 和 Node.js

```bash
# Python 3.10+
sudo apt install -y python3 python3-pip python3-venv

# Node.js 18+（推荐使用 NodeSource 或 nvm）
curl -fsSL https://deb.nodesource.com/setup_22.x | sudo -E bash -
sudo apt install -y nodejs
npm --version  # 验证
```

### 2.5 安装 Protobuf 编译器

```bash
sudo apt install -y protobuf-compiler libprotobuf-dev
protoc --version  # 验证是否 >= 3.15
```

---

## 3. Ubuntu 服务器：克隆代码并构建

### 3.1 克隆项目

```bash
cd ~
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver

# 查看所有分支
git branch -a

# 切换到 AI Agent 功能分支（带 MCP 官方 SDK 升级）
git checkout feat/upgrade-mcp-official-sdk
```

### 3.2 构建 C++ ChatServer

```bash
mkdir -p build && cd build
cmake ..
make -j$(nproc)

# 编译成功后，可执行文件在 bin/ 目录
ls ../bin/ChatServer
```

### 3.3 安装 Python Bridge 依赖

```bash
pip3 install -r frontend/bridge/requirements.txt
```

### 3.4 安装 AI Agent 依赖（可选，不使用 AI 可跳过）

```bash
pip3 install -r agent_service/requirements.txt
```

### 3.5 安装前端 Node 依赖

```bash
cd frontend/web
npm install
cd ../..
```

---

## 4. Ubuntu 服务器：启动基础设施（Docker）

### 4.1 检查端口冲突

项目会启动 12+ 个容器，确保以下端口未被占用：

```bash
# 检查关键端口
sudo lsof -i :3306   # MySQL 主库
sudo lsof -i :6379   # Redis
sudo lsof -i :9092   # Kafka
sudo lsof -i :9093   # Kafka EXTERNAL

# 如果 3306 被宿主机 MySQL 占用，先停掉
sudo systemctl stop mysql
sudo systemctl disable mysql
```

### 4.2 启动 Docker 容器

```bash
cd ~/chatserver

# 首次启动会拉取镜像 + 初始化数据库，耗时 5-10 分钟
docker compose up -d

# 等待所有容器就绪（约 30 秒）
sleep 10
docker compose ps
```

正常输出应该看到 **12 个容器全部显示 "Up"**：

| 容器名 | 状态 |
|--------|------|
| chat_mysql_master | Up |
| chat_mysql_slave1 | Up |
| chat_mysql_slave2 | Up |
| chat_redis | Up |
| chat_redis_slave1 | Up |
| chat_redis_slave2 | Up |
| chat_redis_sentinel1 | Up |
| chat_redis_sentinel2 | Up |
| chat_redis_sentinel3 | Up |
| chat_zookeeper | Up |
| chat_kafka | Up |
| chat_nginx | Up |

### 4.3 验证数据库初始化成功

```bash
# 查看数据库中的表和数据
docker exec chat_mysql_master mysql -h127.0.0.1 -uroot -p'Sf523416&111' chat -e "SELECT id, name, state FROM user;"
```

应该看到预置的 5 个用户（包含 ID=10000 的 AI 智能助手）。

---

## 5. Ubuntu 服务器：启动服务器进程

### 5.1 启动 ChatServer（3 个实例）

```bash
cd ~/chatserver

export KAFKA_HOST=localhost
export KAFKA_PORT=9093

# 实例 1（带 MCP AI 接口）
SERVER_PORT=6000 nohup ./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888 > /tmp/server0.log 2>&1 &
sleep 2

# 实例 2
SERVER_PORT=6001 nohup ./bin/ChatServer 0.0.0.0 6001 > /tmp/server1.log 2>&1 &
sleep 2

# 实例 3
SERVER_PORT=6002 nohup ./bin/ChatServer 0.0.0.0 6002 > /tmp/server2.log 2>&1 &
sleep 2

# 验证 3 个端口都在监听
ss -tlnp | grep 600[0-2]
```

> ⚠️ `SERVER_PORT` 环境变量**必须设置**，用于 Kafka 消费者组 ID，确保每个服务器独立接收跨服消息。

### 5.2 重置 AI 用户登录状态

如果之前运行过旧版，AI 用户的数据库状态可能是 "online"，需要重置：

```bash
docker exec chat_mysql_master mysql -h127.0.0.1 -uroot -p'Sf523416&111' chat -e "UPDATE user SET state='offline' WHERE id=10000;"
```

### 5.3 启动 Python Bridge

```bash
cd ~/chatserver
nohup uvicorn frontend.bridge.main:app --host 0.0.0.0 --port 8000 > /tmp/bridge.log 2>&1 &

# 验证 Bridge 启动成功
curl -s http://127.0.0.1:8000/api/me/9999
# 应返回 {"detail":"Not logged in"}（正常，因为 9999 未登录）
```

---

## 6. Ubuntu 服务器：启动 AI 智能助手（可选）

> AI 智能助手依赖 ModelScope API，需要先申请 API Key。
> 如果不启动 AI 助手，服务器和前端仍可正常聊天，只是没有 AI 回复。

### 6.1 申请 API Key

1. 打开 https://www.modelscope.cn
2. 注册/登录账号
3. 进入"模型服务"→"API Key"页面
4. 创建一个 API Key，复制保存

### 6.2 （可选）申请联网搜索 Key

如果需要联网搜索功能（AI 可以查天气、新闻等）：

1. 打开 https://tavily.com
2. 注册免费账号
3. 在 Dashboard 获取 API Key

### 6.3 启动 AI Agent

```bash
cd ~/chatserver

# 请将下面的 API Key 替换为你自己的
export MODELSCOPE_API_KEY="ms-你的APIKEY"
export TAVILY_API_KEY="tvly-你的APIKEY"   # 可选，不设置则无法联网搜索

setsid nohup python3 agent_service/main.py > /tmp/agent.log 2>&1 &
sleep 5
tail -10 /tmp/agent.log
```

正常启动日志：
```
2026-06-03 [INFO] agent_core: Tavily web search enabled
2026-06-03 [INFO] agent_core: Loaded 8 MCP tools via Official SDK
2026-06-03 [INFO] agent_core: Creating LLM with model: MiniMax/MiniMax-M1-80k
2026-06-03 [INFO] agent_core: LangGraph agent compiled with 9 tools
2026-06-03 [INFO] tcp_bridge: Login successful, entering receive loop
```

看到 `Login successful` 表示 AI 助手已成功登录到聊天服务器。

### 6.4 测试 AI 助手（通过命令行）

```bash
# 注册新用户，给 AI 发消息
curl -s -X POST http://127.0.0.1:8000/api/register \
  -H "Content-Type: application/json" \
  -d '{"name":"testuser","password":"123"}' | python3 -m json.tool

# 返回示例：
# { "err_num": 0, "user": { "id": 10160, "name": "testuser" } }

# 登录（将 id 替换为返回的 id）
curl -s -X POST http://127.0.0.1:8000/api/login \
  -H "Content-Type: application/json" \
  -d '{"id":10160,"password":"123"}' | python3 -m json.tool

# 查看好友列表（应包含 AI 智能助手）
# (登录返回的 friends 列表中有 ID=10000 的用户)

# 给 AI 发消息
curl -s -X POST http://127.0.0.1:8000/api/send_message \
  -H "Content-Type: application/json" \
  -d '{"id":10160,"toid":10000,"message":"你好！今天天气怎么样？"}' | python3 -m json.tool

# 等待 5-10 秒，查看聊天历史
curl -s -X POST http://127.0.0.1:8000/api/chat_history \
  -H "Content-Type: application/json" \
  -d '{"id":10160,"peer_id":10000,"chat_type":1,"limit":5}' | python3 -c "
import sys, json
msgs = json.load(sys.stdin).get('messages',[])
for m in msgs:
    f = m.get('fromid')
    t = m.get('message','')[:80]
    print(f'  [from={f}] {t}')
"
```

---

## 7. Windows 电脑：启动前端

### 7.1 准备环境

1. **安装 Node.js**：https://nodejs.org （下载 LTS 版本 20+）
   - 安装完成后打开命令提示符（cmd）或 PowerShell
   - 验证：`node --version` 应显示 v20.x.x

2. **确保 Ubuntu 服务器和 Windows 在同一网络**（如同一局域网，或服务器有公网 IP）

### 7.2 获取 Ubuntu 服务器 IP

在 Ubuntu 上执行：

```bash
ip addr show | grep 'inet ' | grep -v 127.0.0.1
```

记下 IP 地址（例如 `192.168.1.100`）。

### 7.3 克隆项目到 Windows

```cmd
cd C:\
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver

:: 切换分支（与服务器保持一致）
git checkout feat/upgrade-mcp-official-sdk
```

### 7.4 安装依赖并构建

```cmd
cd frontend\web
npm install
npm run build
```

构建成功后会出现 `dist/` 目录。

### 7.5 配置服务器地址（重要）

Windows 浏览器访问的是 Ubuntu 服务器上的 Bridge 服务，需要配置正确的 IP 地址。

**方法一：构建时指定（推荐）**

```cmd
:: 在 frontend\web 目录下
set VITE_BRIDGE_URL=http://192.168.1.100:8000
npx serve dist -l 3000
```

**方法二：修改环境变量文件**

创建 `frontend\web\.env` 文件：

```
VITE_BRIDGE_URL=http://192.168.1.100:8000
```

然后：

```cmd
cd frontend\web
:: 如果之前 build 过，需要重新 build 使 .env 生效
npm run build
npx serve dist -l 3000
```

### 7.6 打开浏览器

在 Windows 浏览器中打开：`http://localhost:3000`

---

## 8. 验证所有服务正常

### 8.1 页面验证

1. 浏览器打开 `http://localhost:3000`
2. 能看到登录/注册页面
3. 点击 **Register** 注册一个新用户
4. 登录后能看到左侧好友列表（包含 "AI智能助手"）
5. 点击 "AI智能助手" 打开聊天窗口
6. 输入消息并发送，等待 AI 回复（约 5-15 秒）
7. AI 回复应该包含 Markdown 格式（如 **粗体**、列表等）

### 8.2 命令行运行测试脚本

在 Ubuntu 上执行完整测试集：

```bash
cd ~/chatserver

# 1. 本机功能测试（61 项）
bash test_full.sh

# 2. AI 助手端到端测试（10 项，需要 AI Agent 已启动）
bash test_agent_e2e.sh
```

全部通过应显示：
- `全部 61 个测试通过！`
- `全部 10 个 Agent 测试通过！`

---

## 9. 常见问题排查

### 问题 1："this account is using, input another!"

**原因**：AI 用户（ID=10000）在数据库中状态为 "online"，但实际进程已退出。

**解决**：
```bash
docker exec chat_mysql_master mysql -h127.0.0.1 -uroot -p'Sf523416&111' chat -e \
  "UPDATE user SET state='offline' WHERE id=10000;"
# 然后重启 Agent
killall python3  # 或 ps aux | grep agent 找到进程 kill
export MODELSCOPE_API_KEY="ms-你的KEY"
setsid nohup python3 agent_service/main.py > /tmp/agent.log 2>&1 &
```

### 问题 2：前端连不上 Bridge（ERR_CONNECTION_REFUSED）

**原因**：VITE_BRIDGE_URL 配置的 IP/端口不对，或 Ubuntu 防火墙未放行。

**解决**：
```bash
# Ubuntu 上检查 Bridge 是否在监听
ss -tlnp | grep 8000

# 检查防火墙
sudo ufw status
sudo ufw allow 8000/tcp  # 放行 Bridge 端口
sudo ufw allow 8888/tcp  # 放行 MCP 端口（AI Agent 需要）
```

### 问题 3：AI 回复日期错误

**原因**：这种情况通常出现在系统 prompt 中没有指定当前日期，或 LLM 模型不知道今天的日期。

**修复**：从最新代码拉取，系统 prompt 已包含当前日期时间。

### 问题 4：聊天消息顺序错乱

**原因**：前端消息排序问题。

**修复**：从最新代码拉取，前端已使用 `useMemo` + `sort` 保证按时间升序显示。

### 问题 5：AI Agent "MCP tools failed to load"

**原因**：MCP 服务器返回的 `capabilities` 为 `null`，老版本官方 SDK 不兼容。

**解决**：拉取代码重新编译 ChatServer（已在 `chat_mcp_server.cpp` 中修复）：
```bash
cd ~/chatserver/build
cmake .. && make -j$(nproc)
# 重启服务器
killall ChatServer
# 按照第 5 节的步骤重新启动
```

### 问题 6：编译报错缺少 muduo 或 protobuf

**原因**：这些库需要从源码编译（apt 安装的版本可能不匹配）。

**解决**：

```bash
# 编译 muduo
cd ~
git clone --depth 1 https://github.com/chenshuo/muduo.git
cd muduo
cmake . && make -j$(nproc)
sudo make install

# 编译 protobuf（项目可能需要 v3.15+）
cd ~
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.15.8/protobuf-cpp-3.15.8.tar.gz
tar -xzf protobuf-cpp-3.15.8.tar.gz
cd protobuf-3.15.8
./configure && make -j$(nproc)
sudo make install
sudo ldconfig
```

### 问题 7：Kafka 连接失败

```bash
# 检查 Kafka 是否正常运行
docker compose logs kafka | tail -20

# 检查端口映射
ss -tlnp | grep 9093

# 确保环境变量设置正确
echo "KAFKA_HOST=$KAFKA_HOST KAFKA_PORT=$KAFKA_PORT"
# 应该输出: KAFKA_HOST=localhost KAFKA_PORT=9093
```

---

## 10. 服务端口速查表

| 服务 | 端口 | 说明 |
|------|------|------|
| MySQL 主库 | 3306 | 持久化存储 |
| MySQL 从库1 | 3307 | 只读副本 |
| MySQL 从库2 | 3308 | 只读副本 |
| Redis 主库 | 6379 | 缓存 |
| Redis 从库1 | 6380 | 缓存副本 |
| Redis 从库2 | 6381 | 缓存副本 |
| Redis Sentinel1 | 26379 | 高可用监控 |
| Kafka (内部) | 9092 | Docker 网络内部使用 |
| Kafka (外部) | **9093** | 宿主机访问 Kafka |
| Zookeeper | 2181 | Kafka 协调 |
| **ChatServer 1** | **6000** | 聊天服务器主实例（带 MCP 8888） |
| ChatServer 2 | 6001 | 聊天服务器实例 2 |
| ChatServer 3 | 6002 | 聊天服务器实例 3 |
| **MCP 接口** | **8888** | AI Agent 调用的工具接口 |
| Nginx 负载均衡 | 7000 | TCP 聊天连接负载均衡 |
| **Bridge API** | **8000** | **前端连接的入口** |
| 前端 Dev | 5173 | Vite 开发服务器 |
| 前端 Prod | 3000 | npx serve 生产模式 |

---

## 启动速查（完整流程）

```bash
# ===== Ubuntu 后端 =====

# 0. 确保 Docker 已启动
sudo systemctl start docker

# 1. 启动基础设施
cd ~/chatserver
docker compose up -d

# 2. 构建 C++ 服务端
cd build && cmake .. && make -j$(nproc) && cd ..

# 3. 重置 AI 用户状态
docker exec chat_mysql_master mysql -h127.0.0.1 -uroot -p'Sf523416&111' chat -e \
  "UPDATE user SET state='offline' WHERE id=10000;"

# 4. 启动 3 个 ChatServer
export KAFKA_HOST=localhost
export KAFKA_PORT=9093
SERVER_PORT=6000 nohup ./bin/ChatServer 0.0.0.0 6000 --mcp-port 8888 > /tmp/server0.log 2>&1 &
SERVER_PORT=6001 nohup ./bin/ChatServer 0.0.0.0 6001 > /tmp/server1.log 2>&1 &
SERVER_PORT=6002 nohup ./bin/ChatServer 0.0.0.0 6002 > /tmp/server2.log 2>&1 &

sleep 3

# 5. 启动 Bridge
nohup uvicorn frontend.bridge.main:app --host 0.0.0.0 --port 8000 > /tmp/bridge.log 2>&1 &

# 6. 启动 AI Agent（需先设置 API Key）
export MODELSCOPE_API_KEY="ms-你的KEY"
export TAVILY_API_KEY="tvly-你的KEY"    # 可选
setsid nohup python3 agent_service/main.py > /tmp/agent.log 2>&1 &

sleep 3

# 7. 测试接口
curl -s http://127.0.0.1:8000/api/me/9999

echo ""
echo "✅ 后端全部启动完毕！"
echo "在 Windows 上打开 http://localhost:3000 开始使用"


# ===== Windows 前端 =====

# 0. 安装 Node.js (https://nodejs.org)

# 1. 克隆代码
cd C:\
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver
git checkout feat/upgrade-mcp-official-sdk

# 2. 安装依赖并构建
cd frontend\web
npm install
npm run build

# 3. 启动前端（将 IP 替换为你的 Ubuntu 服务器 IP）
set VITE_BRIDGE_URL=http://192.168.1.100:8000
npx serve dist -l 3000

# 4. 浏览器打开 http://localhost:3000
```

---

> 如有问题或建议，请在 GitHub Issues 中反馈。
