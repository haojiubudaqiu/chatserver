# 高性能集群聊天服务器

基于 muduo 网络库开发的 C++ 集群聊天服务器，采用 Protobuf 二进制序列化协议，通过 Kafka 消息队列实现多服务器间的消息传递，通过 Redis 缓存热点数据提升读取性能。

## 项目概述

- 用户注册、登录、注销
- 好友管理（添加好友、查看好友列表）
- 群组管理（创建群组、加入群组、群组聊天）
- 私聊消息与群聊消息
- 离线消息存储与登录推送
- 多服务器集群部署，Nginx TCP 负载均衡

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
   │ (group_6000)│   │ (group_6001)│   │ (group_6002)│
   └──────┬──────┘   └──────┬──────┘   └──────┬──────┘
          │                  │                  │
          └──────────────────┼──────────────────┘
                             │
          ┌──────────────────┼──────────────────┐
          │                  │                  │
   ┌──────▼──────┐   ┌──────▼──────┐   ┌──────▼──────┐
   │   Redis     │   │   MySQL     │   │   Kafka     │
   │   纯缓存     │   │ (主从)     │   │   跨服通信    │
   └─────────────┘   └─────────────┘   └─────────────┘
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
         各自在 _userConnMap 中查找 B 的连接
           → 找到 B 的服务器转发消息给 B
```

### 技术栈

| 层 | 技术 | 用途 |
|----|------|------|
| 网络 | muduo | Reactor 模式非阻塞 I/O |
| 序列化 | Protobuf | 二进制消息编码，替代 JSON |
| 持久化 | MySQL 8.0 | 用户/好友/群组/离线消息存储 |
| MySQL 高可用 | 主从复制 + 读写分离 | `DatabaseRouter` 自动路由 |
| 连接池 | ConnectionPool | 主/从独立连接池，连接复用 |
| 缓存 | Redis + Sentinel | 热点数据缓存，哨兵自动故障转移 |
| 跨服通信 | Kafka | 消息广播到所有服务器 |
| 负载均衡 | Nginx TCP Stream | 长连接 TCP 负载分发 |
| 日志 | 异步 Logger | muduo 风格双缓冲异步日志 |

## 项目结构

```
chatserver/
├── src/server/
│   ├── main.cpp                  # 入口，消息处理器注册
│   ├── chatserver.cpp            # 网络层
│   ├── chatservice.cpp           # 业务逻辑层
│   ├── db/                       # 数据库层（连接池、读写分离路由）
│   ├── model/                    # 数据模型（User, Group, Friend, OfflineMsg）
│   ├── redis/                    # Redis 缓存（缓存管理、Redis Sentinel）
│   ├── kafka/                    # Kafka 消息队列（生产者、消费者、管理器）
│   ├── log/                      # 异步日志系统
│   └── proto/                    # Protobuf 消息定义
├── src/client/                   # 客户端实现
├── include/server/               # 头文件（镜像 src/server/ 结构）
├── test/                         # 测试代码
├── docker/mysql/                 # MySQL 初始化脚本与配置
├── docker-compose.yml            # Docker Compose 编排
├── Dockerfile.server             # 服务器镜像
├── Dockerfile.nginx              # Nginx 镜像
└── nginx.conf                    # Nginx 配置
```

## 编译与运行

### 依赖安装

```bash
sudo apt install build-essential cmake libmysqlclient-dev \
  libhiredis-dev libprotobuf-dev protobuf-compiler librdkafka-dev
```

muduo 需从源码安装：
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

### Docker 部署

```bash
docker-compose up -d
```

启动 15 个容器：

| 服务 | 容器数 | 端口 |
|------|--------|------|
| MySQL (1 主 2 从) | 3 | 3306/3307/3308 |
| Redis (1 主 2 从) | 3 | 6379/6380/6381 |
| Redis Sentinel | 3 | 26379/26380/26381 |
| Zookeeper + Kafka | 2 | 2181/9092 |
| ChatServer | 3 | 6000/6001/6002 |
| Nginx | 1 | 7000/8080 |

客户端连接 Nginx 负载均衡入口：`<服务器IP>:7000`

## 核心模块说明

### 缓存层 (Redis)

- **RedisCache**：封装 hiredis，提供用户/好友/群组/状态的缓存读写
- **CacheManager**：高维缓存管理器，统一管理 TTL 失效策略
- **RedisSentinel**：哨兵模式连接，支持主节点故障自动切换
- 缓存 TTL：用户信息 30min、好友列表 15min、群组信息 10min、用户状态 5min

### 数据库层 (MySQL)

- **MySQL**：封装 C API，支持主从角色标识
- **ConnectionPool**：主库 + 从库独立连接池，最大各 10 连接
- **DatabaseRouter**：写操作 → 主库，读操作 → 从库，支持 `forceMaster` 强一致性读

### 消息队列层 (Kafka)

- **KafkaProducer**：librdkafka 封装，消息生产
- **KafkaConsumer**：独立消费线程，消息回调分发
- **KafkaManager**：单例管理器，每个服务器使用唯一 `group.id` 实现广播消费

### 异步日志

- 双缓冲队列设计，日志写入不阻塞业务线程
- 支持日志文件自动轮转

## 性能指标

| 指标 | 数值 |
|------|------|
| QPS | 10,000+ |
| 并发连接 | 1,000+ |
| 私聊消息吞吐 | 1,300+ 条/秒 |
| 缓存响应延迟 | < 10ms |
| 水平扩展 | Nginx + Kafka 广播模型 |
| 数据库读性能 | 主从分离，提升 2-3× |

## License

MIT