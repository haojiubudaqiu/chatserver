# ChatServer 集群聊天服务器 — 从零搭建完整指南

## 环境要求

- Ubuntu 20.04 / 22.04 / 24.04 (建议桌面版，方便多开终端)
- 至少 4GB 内存，20GB 磁盘
- 可访问互联网（镜像源畅通）

---

## 第一步：安装 Docker 和 Docker Compose

```bash
# 更新 apt
sudo apt update && sudo apt upgrade -y

# 安装 Docker
sudo apt install -y docker.io docker-compose-v2

# 启动 Docker
sudo systemctl enable docker
sudo systemctl start docker

# 将当前用户加入 docker 组（避免每次 sudo）
sudo usermod -aG docker $USER

# 退出终端重新登录，然后测试
docker ps
```

> 注意：执行 `usermod` 后**必须重新登录**（`exit` 关掉终端再打开），docker 命令才不需要 `sudo`。

---

## 第二步：克隆项目代码

```bash
cd ~
git clone https://github.com/haojiubudaqiu/chatserver.git
cd chatserver

# 切换到修复后的 clean 分支
git checkout clean
```

---

## 第三步：检查 Docker 和端口冲突

项目会启动 **15 个容器**，占用大量端口。请确保以下端口被释放：

```bash
# 检查关键端口是否被占用
sudo lsof -i :3306   # MySQL 主库
sudo lsof -i :6379   # Redis
sudo lsof -i :9092   # Kafka
sudo lsof -i :8000   # Nginx
sudo lsof -i :6000   # ChatServer 1
```

如果 3306 被宿主机MySQL占用，先停掉它：
```bash
sudo systemctl stop mysql
sudo systemctl disable mysql
```

---

## 第四步：一键启动集群

```bash
# 编译 + 启动所有容器（首次需要下载依赖，可能耗时 20-30 分钟）
docker compose up -d --build
```

---

## 第五步：验证所有容器正常启动

```bash
# 查看所有容器状态（15 个都应该显示 Up）
docker compose ps

# 实时查看服务端日志
docker compose logs -f chatserver

# 预期的正常日志输出：
# - Muduo: "ChatServer" listening on port 6000/6002
# - MySQL: Connected to database successfully
# - Redis: Connect redis-server success!
# - Kafka: Subscribed to Kafka topic: user_messages
# - Kafka: Subscribed to Kafka topic: group_messages
# - Unit tests: All tests passed

# 查看 Nginx（负载均衡器）日志
docker compose logs nginx
# 应该看到连接建立日志
```

> 如果你看到 `Unit tests passed` 字样，说明我们的修复编译正确。

---

## 第六步：启动客户端，测试聊天

### 终端 1 — 用户 Alice

```bash
# 进入 ChatClient 容器
docker compose exec chatclient bash

# 运行客户端，连接 Nginx 负载均衡（8000 端口）
./bin/ChatClient 127.0.0.1 8000
```

进入菜单后：

1. 先输入 **2**（注册），用户名 `alice`，密码 `123456`
   - 系统返回 **"name register success, userid is 1"**
2. 输入 **1**（登录），输入 `userid: 1`，密码 `123456`
3. 看到好友列表和群组列表后，进入主菜单

### 终端 2 — 用户 Bob（新开一个终端窗口）

```bash
# 同样进入客户端
docker compose exec chatclient bash
./bin/ChatClient 127.0.0.1 8000
```

1. 输入 **2** 注册，用户名 `bob`，密码 `123456`
   - 系统返回用户 ID（例如 `userid is 2`）
2. 输入 **1** 登录
3. 在 Alice 终端输入 `addfriend:2` 添加 Bob 为好友

---

## 第七步：功能验证（四个核心测试）

### 测试 1：一对一聊天（跨服务器）

在 Alice 终端输入：
```
chat:2:Hello Bob, this is Alice!
```

预期结果：Bob 的终端立刻收到消息 `[Alice的userid] said: Hello Bob, this is Alice!`

**如果跨服务器，消息会通过 Kafka 广播到另一台 ChatServer 再推送给 Bob。** 我们的修复确保了这条消息不会丢失。

### 测试 2：群组聊天

在 Alice 终端创建群组：
```
creategroup:技术部:技术交流群
```
系统返回 `groupid: 1`

在 Bob 终端加入群组：
```
addgroup:1
```

Alice 发送群消息：
```
groupchat:1:大家好，欢迎加入技术部！
```

预期结果：Bob 和 Alice 都能收到群消息。

### 测试 3：离线消息

1. Bob 终端按下 `Ctrl+C` 断开连接（或直接关闭终端）
2. Alice 发送：`chat:2:Bob, are you there?`
3. 重新启动 Bob 客户端并登录
4. **预期结果：** Bob 一登录立刻弹出离线消息 `[时间戳] [1] said: Bob, are you there?`

**如果这条消息丢失**（以前版本的 bug），说明 Kafka 离线回退有问题——我们已经修复了这个 bug，消息应该被正确存储并从 MySQL 拉取。

### 测试 4：高并发与连接池

在 Alice 终端快速连续发送多条消息：
```
chat:2:message1
chat:2:message2
chat:2:message3
```

预期结果：所有消息按序到达，不会因 TCP 粘包导致 Protobuf 解析崩溃。

---

## 第八步：查看性能指标

```bash
# 查看 MySQL 连接数（验证连接池正常工作，不会无限增长）
docker compose exec mysql bash -c "mysql -uroot -p123456 -e 'show processlist;'"

# 查看 Redis 缓存命中
docker compose exec redis redis-cli INFO stats | grep hits

# 查看 Kafka 消息积压
docker compose exec kafka bash -c "kafka-consumer-groups --bootstrap-server localhost:9092 --group chat_server_group_6000 --describe"
```

---

## 故障排查

### 问题 1：编译失败
```bash
# 单独重建 ChatServer
docker compose build --no-cache chatserver
docker compose up -d chatserver
```

### 问题 2：客户端连接不上
```bash
# 检查 Nginx
docker compose logs nginx

# 手动验证端口
telnet 127.0.0.1 8000
```

### 问题 3：MySQL 连接拒绝
```bash
# 查看 MySQL 日志
docker compose logs mysql_master
```

---

## 架构总结

```
客户端 ──→ Nginx (8000) ──→ ChatServer1 (6000) ──→ 用户A
                │                  │
                │          ┌───────┴───────┐
                │     (Kafka广播)    (Redis缓存)
                │          │               │
                └──→ ChatServer2 (6002) ──→ 用户B
                           │
                     (MySQL主从读写分离)
```

- **Nginx**: 四层负载均衡，分发客户端连接
- **ChatServer**: 基于 Muduo 的业务服务器，多 Pod 实例
- **Kafka**: 跨服务器消息传递（替代了旧版 Redis Pub/Sub）
- **Redis**: 仅用于热点数据缓存（用户信息、群组信息）
- **MySQL**: 主从架构，DatabaseRouter 自动读写分离
