# 高性能集群聊天服务器

## 项目概述

基于 muduo 网络库开发的 C++ 集群聊天服务器，采用 Protobuf 序列化协议，支持用户注册登录、好友管理、群组聊天、私聊消息、离线消息存储等核心即时通讯功能。通过 Redis 缓存、MySQL 连接池、异步日志等优化手段，实现高并发、高性能的服务能力。

## 核心功能

- 用户注册与登录（Protobuf 协议）
- 好友管理（添加好友、查看好友列表）
- 群组管理（创建群组、加入群组、群组聊天）
- 私聊消息与群聊消息
- 离线消息存储与推送
- 集群部署支持（Redis Pub/Sub 跨服务器消息分发）
- Nginx TCP 负载均衡

## 技术架构

### 技术栈

| 层级 | 技术 |
|------|------|
| 网络框架 | muduo (C++ Reactor 模式) |
| 序列化 | Protobuf |
| 数据库 | MySQL 8.0（连接池 + 读写分离） |
| 缓存 | Redis（热点数据缓存 + Sentinel 高可用） |
| 消息队列 | Redis Pub/Sub / Kafka（可选） |
| 负载均衡 | Nginx TCP Stream |
| 容器化 | Docker / Docker Compose |
| 构建工具 | CMake |

### 系统架构

```
                        客户端 (Clients)
                             │
                    ┌────────▼────────┐
                    │  Nginx (TCP LB) │  ← 端口 7000
                    └────────┬────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │ ChatServer  │   │ ChatServer  │   │ ChatServer  │
   │  :6000      │   │  :6001      │   │  :6002      │
   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
          │                  │                  │
          └──────────────────┼──────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │   Redis     │   │   MySQL     │   │   Kafka     │
   │ (缓存/Pub)  │   │ (持久化)    │   │ (消息队列)  │
   └─────────────┘   └─────────────┘   └─────────────┘
```

## 项目结构

```
chatserver/
├── src/                        # 源代码
│   ├── server/                 # 服务器实现
│   │   ├── main.cpp            # 入口
│   │   ├── chatserver.cpp      # 网络层 (muduo)
│   │   ├── chatservice.cpp     # 业务逻辑层
│   │   ├── db/                 # 数据库层 (连接池、路由)
│   │   ├── model/              # 数据模型 (User, Group, Friend, OfflineMsg)
│   │   ├── redis/              # Redis 缓存 + Sentinel
│   │   ├── kafka/              # Kafka 消息队列
│   │   ├── log/                # 异步日志系统
│   │   └── proto/              # Protobuf 定义
│   └── client/                 # 客户端实现
├── include/                    # 头文件
├── test/                       # 测试代码
├── docker/                     # Docker 配置 (MySQL 初始化脚本)
├── thirdparty/                 # 第三方库 (json.hpp)
├── docker-compose.yml          # Docker Compose 编排
├── Dockerfile.server           # 服务器镜像
├── Dockerfile.nginx            # Nginx 镜像
├── nginx.conf                  # Nginx 配置
├── CMakeLists.txt              # 构建配置
├── autobuild.sh                # 自动构建脚本
└── README.md
```

## 编译与运行

### 依赖

```bash
# Ubuntu/Debian
sudo apt install build-essential cmake libmysqlclient-dev \
  libhiredis-dev libprotobuf-dev protobuf-compiler librdkafka-dev
```

muduo 网络库需从源码安装：
```bash
git clone https://github.com/chenshuo/muduo.git
cd muduo && ./build.sh && sudo make install
```

### 编译

```bash
cd build && cmake .. && make
# 或 ./autobuild.sh
```

### 单机运行

```bash
./bin/ChatServer 127.0.0.1 6000
```

### Docker 部署（推荐）

```bash
# 启动全部服务（MySQL + Redis + Kafka + 3个ChatServer + Nginx）
docker-compose up -d

# 客户端连接负载均衡入口：<服务器IP>:7000
```

Docker 会启动以下服务：

| 服务 | 端口 | 说明 |
|------|------|------|
| MySQL Master | 3306 | 主库（写） |
| MySQL Slave1 | 3307 | 从库（读） |
| MySQL Slave2 | 3308 | 从库（读） |
| Redis | 6379 | 缓存 + Pub/Sub |
| Redis Slave1/2 | 6380/6381 | 从节点 |
| Redis Sentinel1-3 | 26379-26381 | 哨兵高可用 |
| Zookeeper | 2181 | Kafka 依赖 |
| Kafka | 9092 | 消息队列 |
| ChatServer ×3 | 6000-6002 | 聊天服务器 |
| Nginx | 7000 | 负载均衡入口 |

## 性能指标

- **处理能力**：支持 10K+ QPS
- **并发连接**：支持 1000+ 并发用户
- **消息吞吐**：支持 1300+ 条/秒私聊消息
- **响应延迟**：缓存数据响应 < 10ms
- **扩展性**：支持水平扩展，多服务器集群部署
- **数据库**：连接池 + 主从复制，读取性能提升 2-3 倍

## License

MIT