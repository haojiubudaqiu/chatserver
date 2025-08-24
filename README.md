# chatserver

在 Linux 环境下基于 muduo 开发的高性能集群聊天服务器。实现新用户注册、用户登录、添加好友、添加群组、好友通信、群组聊天、保持离线消息等功能。

## 功能特性

- 用户注册与登录
- 好友管理（添加好友、查看好友列表）
- 群组管理（创建群组、加入群组、群组聊天）
- 私聊与群聊消息
- 离线消息存储与推送
- 集群部署支持（多服务器负载均衡）
- Redis缓存优化
- Protobuf消息序列化
- 异步日志系统

## 升级特性

### 性能优化
- Redis缓存层：缓存用户信息、好友列表、群组信息等热点数据
- 异步日志系统：非阻塞日志写入，提高系统响应速度
- 连接池优化：MySQL数据库连接池管理

### 高可用性
- Kafka消息队列：替代Redis实现更可靠的消息传递
- MySQL主从复制：数据备份与读写分离
- Nginx负载均衡：支持多服务器集群部署

### 技术升级
- Protobuf序列化：替代JSON，提高消息传输效率
- 异步I/O：基于muduo网络库的高性能异步处理
- 线程池优化：动态线程管理与负载均衡

## 编译方式

```bash
# 标准编译
cd build
rm -rf *
cmake ..
make

# 或使用自动化脚本
./autobuild.sh
```

## 运行方式

```bash
# 启动单个服务器实例
./bin/ChatServer 127.0.0.1 6000

# 启动多个实例配合Nginx负载均衡
./bin/ChatServer 127.0.0.1 6000
./bin/ChatServer 127.0.0.1 6001
./bin/ChatServer 127.0.0.1 6002
```

## 测试方式

```bash
# 运行所有测试
./run_tests.sh

# 或单独运行特定测试
cd build
./test/protobuf_test
./test/redis_cache_test
./test/performance_test
```

## Docker容器化部署

```bash
# 构建所有服务
docker-compose build

# 启动所有服务
docker-compose up -d

# 查看服务状态
docker-compose ps
```

## 配置要求

### 依赖库
- muduo网络库
- MySQL客户端库
- Redis客户端库 (hiredis)
- Protobuf库
- Kafka客户端库 (librdkafka) - 可选

### 数据库配置
- MySQL数据库
- Redis服务器
- Kafka集群 (可选)

## 性能指标

- 支持10K+ QPS
- 支持1000+并发用户连接
- 支持每秒1300+条私聊消息处理
- 响应时间小于10ms (缓存数据)

## 详细文档

- [部署指南](docs/DEPLOYMENT_GUIDE.md) - 详细的部署说明
- [Nginx负载均衡配置](docs/NGINX_LOAD_BALANCING.md) - Nginx配置指南
- [性能调优](docs/PERFORMANCE_TUNING.md) - 性能优化建议
- [测试用例](docs/TEST_CASES.md) - 完整的测试用例文档

## 项目结构

```
chatserver/
├── build/              # 构建目录
├── bin/                # 可执行文件
├── src/                # 源代码
│   ├── server/         # 服务器端实现
│   │   ├── db/         # 数据库操作
│   │   ├── model/      # 数据模型
│   │   ├── proto/      # Protobuf定义
│   │   ├── redis/      # Redis操作
│   │   ├── log/        # 日志系统
│   │   └── kafka/      # Kafka集成
│   └── client/         # 客户端实现
├── include/            # 头文件
│   ├── server/         # 服务器头文件
│   └── client/         # 客户端头文件
├── test/               # 测试代码
├── docker/             # Docker相关文件
├── thirdparty/         # 第三方库
└── docs/               # 文档目录
```

## 对于项目中核心类的理解：
https://mp.csdn.net/mp_blog/creation/editor/149334321