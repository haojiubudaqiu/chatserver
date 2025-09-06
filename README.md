# 高性能分布式集群聊天服务器

## 项目概述

在Linux环境下基于muduo网络库开发的高性能分布式集群聊天服务器，支持用户注册登录、好友管理、群组聊天、点对点通信、离线消息存储等核心功能。通过一系列技术创新和架构升级，实现了支持高并发、高可用的即时通讯系统。

## 核心特性

### 基础功能
- 用户注册与登录
- 好友管理（添加好友、查看好友列表）
- 群组管理（创建群组、加入群组、群组聊天）
- 私聊与群聊消息
- 离线消息存储与推送
- 集群部署支持（多服务器负载均衡）

### 性能优化
- **Redis缓存层**：缓存用户信息、好友列表、群组信息等热点数据
- **异步日志系统**：非阻塞日志写入，提高系统响应速度
- **连接池优化**：MySQL数据库连接池管理
- **Protobuf序列化**：替代JSON，提高消息传输效率

### 高可用性
- **Kafka消息队列**：替代Redis实现更可靠的消息传递
- **MySQL主从复制**：数据备份与读写分离
- **Nginx负载均衡**：支持多服务器集群部署

## 技术架构

### 核心技术栈
- **编程语言**：C++11
- **网络框架**：Muduo网络库
- **数据库**：MySQL（主从复制架构）
- **缓存系统**：Redis（多级缓存）
- **消息队列**：Kafka（消息可靠性保障）
- **负载均衡**：Nginx TCP负载均衡
- **序列化**：Protobuf二进制序列化
- **构建工具**：CMake
- **容器化**：Docker Compose

### 系统架构图

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                            客户端层 (Clients)                               │
└─────────────────────────────┬───────────────────────────────────────────────┘
                              │ TCP连接
┌─────────────────────────────▼───────────────────────────────────────────────┐
│                          负载均衡层 (Nginx)                                │
└─────────────────────────────┬───────────────────────────────────────────────┘
                              │ TCP转发
┌─────────────────────────────▼───────────────────────────────────────────────┐
│                       服务器集群层 (Chat Servers)                           │
│                                                                             │
│  ┌─────────────┐  ┌─────────────┐  ┌─────────────┐                          │
│  │ Server 1    │  │ Server 2    │  │ Server 3    │                          │
│  │ (6000端口)  │  │ (6001端口)  │  │ (6002端口)  │                          │
│  └──────┬──────┘  └──────┬──────┘  └──────┬──────┘                          │
│         │                │                │                                 │
│         └────────────────┼────────────────┘                                 │
│                          │                                                  │
│                    ┌─────▼─────┐                                            │
│                    │ 业务逻辑层 │                                            │
│                    │(ChatService)│                                           │
│                    └─────┬─────┘                                            │
│                          │                                                  │
├──────────────────────────┼──────────────────────────────────────────────────┤
│                          │                                                  │
│                    ┌─────▼─────┐         ┌──────────────┐                  │
│                    │ 数据访问层 │◄────────┤   缓存层     │                  │
│                    └─────┬─────┘         │ (Redis Cache)│                  │
│                          │               └──────────────┘                  │
│                ┌─────────┼─────────┐                                       │
│                │         │         │                                       │
│         ┌──────▼──┐ ┌────▼────┐ ┌──▼──────┐                                │
│         │ MySQL   │ │ Redis   │ │ Kafka   │                                │
│         │(Master) │ │(Cache)  │ │(MQ)     │                                │
│         └─────────┘ └─────────┘ └─────────┘                                │
│                                                                             │
│         ┌─────────┐ ┌─────────┐ ┌─────────┐                                │
│         │ MySQL   │ │ MySQL   │ │         │                                │
│         │(Slave1) │ │(Slave2) │ │   ...   │                                │
│         └─────────┘ └─────────┘ └─────────┘                                │
└─────────────────────────────────────────────────────────────────────────────┘
```

### 架构组件详解

#### 1. 网络通信层
- 基于muduo C++网络库构建
- 采用Reactor模式和IO多路复用技术
- 实现高并发网络处理能力

#### 2. 业务逻辑层 (ChatService)
- 核心业务处理模块
- 消息分发和处理
- 用户状态管理
- 好友和群组关系维护

#### 3. 数据访问层
- **用户模型** (`UserModel`)：用户信息的增删改查
- **好友模型** (`FriendModel`)：好友关系管理
- **群组模型** (`GroupModel`)：群组信息管理
- **离线消息模型** (`OfflineMsgModel`)：离线消息存储

#### 4. 缓存层 (Redis Cache)
- 用户信息缓存
- 好友列表缓存
- 群组信息缓存
- 用户状态缓存
- 离线消息计数缓存

#### 5. 消息队列层 (Kafka)
- 跨服务器消息传递
- 消息可靠性保障
- 高吞吐量消息处理

#### 6. 数据库层 (MySQL)
- **主库**：处理写操作和重要读操作
- **从库**：处理读操作，实现读写分离
- **连接池**：优化数据库连接管理

## 项目文件结构

```
chatserver/
├── bin/                    # 编译后的可执行文件
├── build/                  # CMake构建目录
├── src/                    # 源代码目录
│   ├── server/             # 服务器端实现
│   │   ├── main.cpp        # 服务器主程序入口
│   │   ├── chatserver.cpp  # 网络服务类实现
│   │   ├── chatservice.cpp # 业务逻辑类实现
│   │   ├── proto/          # Protobuf消息定义
│   │   ├── db/             # 数据库操作模块
│   │   ├── model/          # 数据模型模块
│   │   ├── redis/          # Redis缓存模块
│   │   ├── kafka/          # Kafka消息队列模块
│   │   ├── log/            # 日志系统模块
│   │   ├── proto_msg_handler.cpp # Protobuf消息处理器
│   │   └── proto_msg_processor.cpp # Protobuf消息处理框架
│   ├── client/             # 客户端实现
│   │   └── main.cpp        # 客户端主程序
│   └── CMakeLists.txt      # 源码构建配置
├── include/                # 头文件目录
│   ├── server/             # 服务器头文件
│   ├── client/             # 客户端头文件
│   └── public.hpp          # 公共头文件
├── test/                   # 测试代码目录
│   ├── protobuf_test.cpp           # Protobuf测试
│   ├── protobuf_processor_test.cpp # Protobuf处理器测试
│   ├── redis_cache_test.cpp        # Redis缓存测试
│   ├── performance_test.cpp        # 性能测试
│   └── testJson/                   # JSON测试代码
├── docker/                 # Docker相关文件
│   └── mysql/              # MySQL初始化脚本
├── docs/                   # 项目文档
│   ├── DEPLOYMENT_GUIDE.md         # 部署指南
│   ├── NGINX_LOAD_BALANCING.md     # Nginx负载均衡配置
│   ├── PERFORMANCE_TUNING.md       # 性能调优指南
│   └── TEST_CASES.md               # 测试用例文档
├── thirdparty/             # 第三方库
│   └── json.hpp            # JSON库
├── CMakeLists.txt          # 项目构建配置
├── nginx.conf              # Nginx配置文件
├── Dockerfile.server       # 服务器Docker镜像配置
├── Dockerfile.nginx        # Nginx Docker镜像配置
├── docker-compose.yml      # Docker Compose配置
├── autobuild.sh            # 自动构建脚本
├── run_tests.sh            # 测试运行脚本
└── README.md               # 项目说明文档
```

## 编译与构建

### 环境要求
- Linux系统 (推荐Ubuntu 18.04+)
- C++11编译器 (推荐GCC 7+)
- CMake 3.10+
- MySQL 5.7+
- Redis 5.0+
- Kafka (可选)
- Protobuf 3.0+

### 依赖库安装
```bash
# Ubuntu/Debian系统
sudo apt update
sudo apt install build-essential cmake libmysqlclient-dev libhiredis-dev libprotobuf-dev protobuf-compiler librdkafka-dev

# 安装muduo网络库
git clone https://github.com/chenshuo/muduo.git
cd muduo
./build.sh
sudo make install
```

### 编译项目
```bash
# 标准编译方式
cd build
cmake ..
make

# 或使用自动化脚本
./autobuild.sh
```

## 运行与部署

### 单机运行
```bash
# 启动单个服务器实例
./bin/ChatServer 127.0.0.1 6000
```

### 集群部署
```bash
# 启动多个实例配合Nginx负载均衡
./bin/ChatServer 127.0.0.1 6000
./bin/ChatServer 127.0.0.1 6001
./bin/ChatServer 127.0.0.1 6002
```

### Docker容器化部署
```bash
# 构建所有服务
docker-compose build

# 启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps
```

## 性能指标

- **处理能力**：支持10K+ QPS
- **并发连接**：支持1000+并发用户连接
- **消息吞吐**：支持每秒1300+条私聊消息处理
- **响应延迟**：缓存数据响应时间小于10ms
- **扩展性**：支持水平扩展，多服务器集群部署

## 测试

### 运行测试
```bash
# 运行所有测试
./run_tests.sh

# 或单独运行特定测试
cd build
./test/protobuf_test
./test/redis_cache_test
./test/performance_test
```

## 项目价值

该项目通过一系列技术创新和架构优化，成功将传统聊天服务器升级为高性能分布式即时通讯平台，具备金融级高可用性和互联网级高性能特征，可作为企业级即时通讯解决方案的基础架构。