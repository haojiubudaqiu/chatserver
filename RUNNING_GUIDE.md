# 聊天服务器运行指南

## 项目概述

这是一个基于muduo网络库开发的高性能集群聊天服务器，支持用户注册、登录、添加好友、创建/加入群组、私聊、群聊、离线消息等功能。

## 运行环境要求

### 依赖服务
- MySQL 8.0+ 数据库
- Redis 7+ 服务
- Kafka服务（可选）

### 系统依赖库
- muduo网络库
- MySQL客户端库
- Redis客户端库 (hiredis)
- Protobuf库
- Kafka客户端库 (librdkafka) - 可选

## 推荐运行方式：使用Docker容器化部署

### 1. 安装Docker和docker-compose
```bash
# Ubuntu/Debian系统
sudo apt update
sudo apt install docker.io docker-compose -y

# 启动Docker服务
sudo systemctl start docker
sudo systemctl enable docker
```

### 2. 启动所有依赖服务
```bash
# 进入项目根目录
cd /home/song/workspace/chatserver

# 启动所有服务（包括MySQL、Redis、Kafka等）
docker-compose up -d

# 查看服务状态
docker-compose ps
```

### 3. 初始化数据库
数据库会在MySQL容器启动时自动初始化，使用`docker/mysql/init.sql`脚本创建表结构和测试数据。

### 4. 编译项目
```bash
# 清理旧的构建文件
cd build
rm -rf *

# 配置和编译
cmake ..
make

# 或使用自动化脚本
cd ..
./autobuild.sh
```

### 5. 运行服务端
```bash
# 启动单个服务器实例
./bin/ChatServer 127.0.0.1 6000

# 或启动多个实例配合负载均衡
./bin/ChatServer 127.0.0.1 6000
./bin/ChatServer 127.0.0.1 6001
./bin/ChatServer 127.0.0.1 6002
```

### 6. 运行客户端
```bash
# 启动客户端连接到服务器
./bin/ChatClient 127.0.0.1 6000
```

## 客户端使用说明

### 1. 登录/注册界面
启动客户端后会显示主菜单：
```
========================
1. login
2. register
3. quit
========================
choice:
```

### 2. 注册新用户
选择选项2，输入用户名和密码进行注册。

### 3. 用户登录
选择选项1，输入用户ID和密码进行登录。

### 4. 聊天主菜单
登录成功后进入聊天主菜单，支持以下命令：

- `help` : 显示所有支持的命令
- `chat:friendid:message` : 一对一聊天
- `addfriend:friendid` : 添加好友
- `creategroup:groupname:groupdesc` : 创建群组
- `addgroup:groupid` : 加入群组
- `groupchat:groupid:message` : 群聊
- `loginout` : 注销

### 5. 使用示例
```
# 添加好友（用户ID为2）
addfriend:2

# 与好友（用户ID为2）聊天
chat:2:你好，我是新用户！

# 创建群组
creategroup:技术交流群:这是一个技术交流的群组

# 加入群组（群组ID为1）
addgroup:1

# 群聊（群组ID为1）
groupchat:1:大家好，我是新加入的成员！
```

## 测试数据

数据库初始化脚本会创建以下测试用户：
- admin / admin123
- user1 / pass123
- user2 / pass456
- user3 / pass789

## 常见问题解决

### 1. 连接数据库失败
检查MySQL服务是否正常运行，确认数据库连接配置是否正确。

### 2. 连接Redis失败
检查Redis服务是否正常运行，确认端口和地址配置。

### 3. 编译错误
确保所有依赖库已正确安装，特别是muduo、MySQL客户端、Redis客户端和Protobuf库。

### 4. 客户端连接失败
检查服务端是否正常运行，确认IP地址和端口号是否正确。

## 性能指标

- 支持10K+ QPS
- 支持1000+并发用户连接
- 支持每秒1300+条私聊消息处理
- 响应时间小于10ms (缓存数据)

## 架构特点

1. **高性能网络层**：基于muduo网络库的单线程Reactor模型
2. **集群支持**：通过Redis实现多服务器间的消息传递
3. **数据持久化**：使用MySQL存储用户、好友、群组等核心数据
4. **缓存优化**：Redis缓存热点数据，提升访问速度
5. **消息队列**：可选的Kafka支持，提供更可靠的消息传递
6. **异步日志**：非阻塞日志系统，不影响主业务流程