# 高性能聊天服务器性能调优指南

本文档详细说明如何对聊天服务器进行性能调优，以达到支持10K+ QPS、1000+并发连接和每秒1300+条私聊消息的目标。

## 1. 系统级性能调优

### 1.1 内核参数调优

编辑 `/etc/sysctl.conf` 文件，添加以下配置：

```bash
# 网络相关参数
net.core.somaxconn = 65535
net.core.netdev_max_backlog = 5000
net.core.rmem_default = 262144
net.core.wmem_default = 262144
net.core.rmem_max = 16777216
net.core.wmem_max = 16777216
net.ipv4.tcp_rmem = 4096 65536 16777216
net.ipv4.tcp_wmem = 4096 65536 16777216
net.ipv4.tcp_mem = 786432 2097152 3145728
net.ipv4.tcp_max_syn_backlog = 65535
net.ipv4.tcp_fin_timeout = 10
net.ipv4.tcp_keepalive_time = 1200
net.ipv4.tcp_max_tw_buckets = 5000
net.ipv4.tcp_tw_reuse = 1
net.ipv4.tcp_tw_recycle = 1
net.ipv4.ip_local_port_range = 1024 65535

# 文件系统相关参数
fs.file-max = 1000000
fs.nr_open = 1000000

# 内存相关参数
vm.swappiness = 1
vm.dirty_ratio = 10
vm.dirty_background_ratio = 5
```

应用配置：
```bash
sudo sysctl -p
```

### 1.2 文件描述符限制

编辑 `/etc/security/limits.conf` 文件：

```bash
* soft nofile 1000000
* hard nofile 1000000
root soft nofile 1000000
root hard nofile 1000000
```

### 1.3 CPU和内存调优

```bash
# 设置CPU性能模式
sudo cpupower frequency-set -g performance

# 禁用NUMA平衡（如果适用）
echo 0 > /proc/sys/kernel/numa_balancing
```

## 2. 应用级性能调优

### 2.1 数据库连接池优化

在 `src/server/db/db.cpp` 中调整连接池参数：

```cpp
// 初始化数据库连接池 - 增加连接数
MySQL::initConnectionPool("127.0.0.1", "root", "password", "chat", 3306, 100);
```

### 2.2 Redis连接优化

在 `src/server/redis/redis.cpp` 中优化连接：

```cpp
// 增加Redis连接池或优化单连接使用
// 使用连接池模式减少连接建立开销
```

### 2.3 线程池优化

在聊天服务器中配置合适的线程数：

```cpp
// 在 main.cpp 中设置合适的线程数
EventLoop loop;
// 根据CPU核心数设置线程数
int threadCount = std::thread::hardware_concurrency();
if (threadCount > 8) threadCount = 8; // 限制最大线程数
```

## 3. 网络层性能优化

### 3.1 TCP参数优化

在代码中设置TCP选项：

```cpp
// 在 ChatServer 构造函数中添加
TcpServer server(loop, addr, "ChatServer");
server.setThreadNum(4); // 设置IO线程数

// 设置TCP选项
server.setTcpNoDelay(true);
server.setTcpKeepAlive(true);
```

### 3.2 缓冲区优化

```cpp
// 优化TCP接收和发送缓冲区
TcpConnection::setTcpNoDelay(true);
// 根据消息大小调整缓冲区
```

## 4. 缓存策略优化

### 4.1 Redis缓存优化

在 `redis_cache.cpp` 中优化缓存策略：

```cpp
// 调整缓存过期时间
// 用户信息缓存：30分钟
reply = (redisReply*)redisCommand(_context, "EXPIRE %s 1800", key.c_str());

// 好友列表缓存：15分钟
reply = (redisReply*)redisCommand(_context, "EXPIRE %s 900", key.c_str());

// 群组信息缓存：10分钟
reply = (redisReply*)redisCommand(_context, "EXPIRE %s 600", key.c_str());

// 用户状态缓存：5分钟
reply = (redisReply*)redisCommand(_context, "EXPIRE %s 300", key.c_str());

// 离线消息计数缓存：2分钟
reply = (redisReply*)redisCommand(_context, "EXPIRE %s 120", key.c_str());
```

### 4.2 缓存预热

```cpp
// 在服务器启动时预热热点数据
void ChatService::warmupCache() {
    // 预加载活跃用户信息
    // 预加载热门群组信息
    // 预加载常用好友关系
}
```

## 5. 数据库性能优化

### 5.1 索引优化

确保数据库表有适当的索引：

```sql
-- 用户表索引
CREATE INDEX idx_user_state ON user(state);
CREATE INDEX idx_user_name ON user(name);

-- 好友关系表索引
CREATE INDEX idx_friend_userid ON friend(userid);
CREATE INDEX idx_friend_friendid ON friend(friendid);

-- 群组用户关系表索引
CREATE INDEX idx_groupuser_userid ON groupuser(userid);
CREATE INDEX idx_groupuser_groupid ON groupuser(groupid);

-- 离线消息表索引
CREATE INDEX idx_offlinemessage_userid ON offlinemessage(userid);
CREATE INDEX idx_offlinemessage_created_at ON offlinemessage(created_at);
```

### 5.2 查询优化

优化SQL查询语句：

```cpp
// 使用预处理语句减少解析开销
// 批量操作减少网络往返
// 连接池复用减少连接建立开销
```

## 6. 消息队列优化

### 6.1 Redis Pub/Sub优化

```cpp
// 在 redis.cpp 中优化发布订阅
// 使用批量发布减少网络开销
// 优化频道管理减少内存占用
```

### 6.2 Kafka集成优化（如果使用）

```cpp
// 配置合适的批处理大小
// 调整压缩算法
// 优化分区策略
```

## 7. 负载均衡优化

### 7.1 Nginx配置优化

在 `nginx.conf` 中优化配置：

```nginx
stream {
    upstream chat_backend {
        # 使用least_conn算法优化负载均衡
        least_conn;
        server chat_server_1:6000 weight=3 max_fails=3 fail_timeout=30s;
        server chat_server_2:6001 weight=3 max_fails=3 fail_timeout=30s;
        server chat_server_3:6002 weight=2 max_fails=3 fail_timeout=30s;
        
        # 启用keepalive连接池
        keepalive 32;
    }
    
    server {
        listen 7000;
        proxy_pass chat_backend;
        proxy_timeout 1s;
        proxy_responses 1;
        
        # 启用代理缓冲
        proxy_buffer_size 16k;
        proxy_buffers 8 16k;
    }
}
```

## 8. 异步日志优化

### 8.1 日志级别控制

```cpp
// 在代码中控制日志级别
// 生产环境使用WARN或ERROR级别
// 避免过多的DEBUG日志影响性能
```

### 8.2 日志异步写入

```cpp
// 使用异步日志系统
// 批量写入减少磁盘IO
// 使用内存映射提高写入性能
```

## 9. 压力测试和监控

### 9.1 性能测试脚本

使用 `performance_test.cpp` 进行压力测试：

```bash
# 编译性能测试程序
cd build && make performance_test

# 运行性能测试
./test/performance_test
```

### 9.2 监控关键指标

```bash
# 监控系统资源
top -p $(pgrep ChatServer)

# 监控网络连接数
ss -s

# 监控数据库连接
mysqladmin processlist

# 监控Redis性能
redis-cli info stats
```

## 10. 性能调优 checklist

### 10.1 网络层
- [ ] TCP参数优化完成
- [ ] 连接池配置合理
- [ ] 缓冲区大小调整
- [ ] KeepAlive设置正确

### 10.2 应用层
- [ ] 线程池大小合理
- [ ] 数据库连接池优化
- [ ] Redis连接池优化
- [ ] 缓存策略调整

### 10.3 数据库层
- [ ] 索引优化完成
- [ ] 查询语句优化
- [ ] 连接池配置合理
- [ ] 主从复制配置

### 10.4 系统层
- [ ] 内核参数调优
- [ ] 文件描述符限制调整
- [ ] CPU和内存调优
- [ ] 磁盘IO优化

### 10.5 负载均衡
- [ ] Nginx配置优化
- [ ] 负载均衡算法选择
- [ ] 健康检查配置
- [ ] 连接池启用

通过系统性地应用这些性能调优措施，聊天服务器应该能够达到设计的性能目标：
- 支持10K+ QPS
- 支持1000+并发用户连接
- 支持每秒1300+条私聊消息处理
- 平均响应时间小于10ms（缓存数据）