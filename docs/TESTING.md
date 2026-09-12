# ChatPulse 测试指南

本文档说明项目的测试体系：每种测试覆盖什么、如何运行、在什么环境运行。

## 测试层级总览

| 层级 | 测试内容 | 需要环境 | 入口 |
|------|----------|----------|------|
| 单元测试 | C++ 数据库连接池/模型/Redis/Kafka | Linux（MySQL/Redis/Kafka 可选） | `run_tests.sh` |
| 单元测试 | Agent 服务（LLM 用 Dummy 模型，无网络） | Python 3.10+ | `cd agent_service && pytest` |
| 单元测试 | Bridge HTTP/WS 接口（mock TCP 层） | Python 3.10+ | `cd frontend/bridge && pytest` |
| 前端构建 | TypeScript 类型检查 + Vite 生产构建 | Node 18+ | `cd frontend/web && npm run build` |
| 集成测试 | 61 项功能测试（真实多服务器） | 全部 Docker 服务运行 | `./test_full.sh` |
| Agent E2E | 10 项 AI 助手端到端测试 | 全部 Docker 服务运行 | `./test_agent_e2e.sh` |
| 跨服测试 | 多服务器消息路由正确性 | 全部 Docker 服务运行 | `./test_cross_server.sh` |
| 持久化测试 | 历史消息/离线消息持久化 | 全部 Docker 服务运行 | `./test_persistence.sh` |
| C++ 单元测试 | 连接池/模型/Redis/Kafka 组件 | Linux 编译产物 | `bin/test_*` |

## 快速开始：一键运行全部单元测试

```bash
./run_tests.sh       # C++（Linux 上）+ Agent + Bridge 单元测试
./run_tests.sh --all # 单元测试 + 全部集成测试（需要服务器运行）
```

## 各层测试详解

### 1. C++ 单元测试（Linux）

```bash
# 先编译（test/CMakeLists.txt 会构建 bin/test_*）
mkdir -p build && cd build && cmake .. && make -j$(nproc) && cd ..
./bin/test_db_pool   # MySQL 连接池：获取/归还/健康检查
./bin/test_models    # Model 层：连接池在无 DB 时优雅降级
./bin/test_redis     # Redis 缓存/哨兵
./bin/test_kafka     # Kafka 生产者/消费者
```

> 说明：这些二进制依赖 Linux 与共享库，需在 Ubuntu 或本项目 Docker 镜像中执行。

### 2. Agent 单元测试

```bash
cd agent_service
pip install -r requirements.txt
python -m pytest tests -q    # 14 项
```

覆盖：协议编解码、TCP Bridge 连接逻辑、Agent 消息处理流水线、三层记忆的 Redis
存取与摘要触发（LLM 全部使用 Dummy 模型，不消耗 API、不依赖网络）。

### 3. Bridge 单元测试

```bash
cd frontend/bridge
pip install -r requirements.txt pytest httpx
python -m pytest tests -q    # 10 项
```

覆盖：健康检查、登录/注销/会话生命周期、未登录访问保护、注册校验、
WebSocket 未登录拒绝、离线消息与群聊消息的 protobuf 解码。
TCP 层通过 mock 隔离，不需要运行中的 ChatServer。

### 4. 前端构建校验

```bash
cd frontend/web
npm install   # 或 npm ci
npm run build # tsc -b && vite build，零错误才算通过
```

### 5. 集成测试（需要全部服务）

```bash
docker compose up -d
# 等待 2-3 分钟全部就绪，然后：
./test_full.sh            # 61 项：注册/登录/私聊/群聊/好友/群组/离线消息/历史
./test_agent_e2e.sh       # 10 项：AI 对话/代发消息/查好友/查在线/搜索
./test_cross_server.sh    # 跨服务器：A 在 6000、B 在 6001，验证 Kafka 路由
./test_persistence.sh     # 持久化：重启后历史消息、离线消息不丢
```

> 集成测试使用真实 3 个 ChatServer + Kafka + MySQL + Redis，会写入数据库测试数据，
> 建议在专用环境运行（`docker compose down -v && docker compose up -d` 可重置）。

## CI（GitHub Actions）

推送后自动执行（`.github/workflows/ci.yml`）：

1. `docker build -f Dockerfile.server` — 在 Ubuntu 上完整编译 C++ 服务端
2. Python 单元测试（Agent + Bridge）
3. 前端 TypeScript 构建
4. `docker compose config` 编排文件校验

## 常见问题

- **`Exec format error`**：在 Windows 上直接运行了 Linux 编译的 `bin/test_*`，请到 Linux 执行。
- **`No module named 'langchain_mcp_adapters'`**：未安装 `agent_service/requirements.txt`。
- **pytest 全部 ERROR**：检查是否在正确的目录运行（`agent_service/` 与 `frontend/bridge/` 各自有 `tests/`）。