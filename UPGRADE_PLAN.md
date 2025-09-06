# High-Performance Chat Server Upgrade Plan

## Project Overview
This document outlines the comprehensive upgrade plan for transforming the existing chat server into a high-performance Instant Messaging service capable of handling 10K+ QPS, 1000+ concurrent connections, and 1300+ private messages per second.

## Target Specifications
- **Platform**: Linux environment
- **Performance**: 10K+ QPS, 1000+ concurrent users, 1300+ private messages/second
- **Technologies**: C++11, MySQL, Redis, Nginx, Kafka, Protobuf, Muduo

## Planned Upgrades

### 1. Serialization Migration: JSON to Protobuf
**Objective**: Replace JSON serialization with more efficient Protobuf binary serialization.

#### Implementation:
- Create `.proto` files defining all message types
- Generate C++ classes using `protoc` compiler
- Update message handlers to process Protobuf instead of JSON
- Implement backward compatibility during transition period
- Update build system to include Protobuf compilation

#### Benefits:
- 3-10x reduction in message size
- 2-5x improvement in serialization/deserialization speed
- Strong typing and schema validation

### 2. Message Queue Replacement: Redis to Kafka
**Objective**: Replace Redis pub/sub with Kafka for improved scalability and reliability.

#### Implementation:
- Integrate `librdkafka` C++ client library
- Create Kafka wrapper classes (KafkaProducer, KafkaConsumer)
- Replace Redis messaging calls with Kafka equivalents
- Configure Kafka cluster for message persistence and replication
- Implement proper error handling and retry mechanisms

#### Benefits:
- Higher throughput and better persistence
- Horizontal scalability
- Strong ordering guarantees
- Better fault tolerance

### 3. Redis Caching Layer Implementation
**Objective**: Add Redis caching to reduce database load and improve response times.

#### Implementation:
- Cache frequently accessed data (users, friends, groups)
- Implement time-based expiration (TTL) strategies
- Add event-based cache invalidation
- Integrate with existing MySQL operations
- Implement fallback mechanisms for Redis failures

#### Benefits:
- 60-80% reduction in database queries
- 5-10x improvement in data retrieval times
- Better horizontal scaling capabilities

### 4. MySQL Database Improvements
**Objective**: Optimize database architecture for high availability and performance.

#### Implementation:
- Implement connection pooling to reduce overhead
- Add proper indexing strategies for all tables
- Set up MySQL master-slave replication
- Use prepared statements to prevent SQL injection
- Implement asynchronous database operations
- Configure character set to UTF8MB4 for internationalization

#### Benefits:
- Reduced connection overhead
- Faster query execution
- High availability through replication
- Improved security

### 5. Nginx Load Balancing Configuration
**Objective**: Configure Nginx for TCP load balancing across chat server instances.

#### Implementation:
- Set up Nginx Stream module for TCP load balancing
- Configure consistent hashing for session persistence
- Implement health checks for server instances
- Configure logging and monitoring
- Set up high availability with multiple Nginx instances

#### Benefits:
- Even distribution of client connections
- Session persistence for user experience
- Automatic failover for high availability
- Detailed monitoring and logging

### 6. Asynchronous Logging System
**Objective**: Implement high-performance async logging to reduce I/O blocking.

#### Implementation:
- Create multi-level logging system (DEBUG, INFO, WARN, ERROR)
- Implement thread-safe logging with lock-free queues
- Add log file rotation based on size and time
- Implement asynchronous I/O for log writing
- Add performance optimization techniques

#### Benefits:
- Non-blocking logging operations
- Better performance under high load
- Efficient disk space management
- Improved system reliability

## Additional Performance Optimizations

### 1. Connection Handling
- Implement connection pooling for better resource management
- Add connection timeout mechanisms
- Tune TCP parameters for high concurrency

### 2. Memory Management
- Implement object pooling for frequently used objects
- Use buffer recycling and zero-copy techniques
- Integrate jemalloc or tcmalloc for better memory allocation

### 3. Thread Pool Optimization
- Implement dynamic thread pool with work-stealing queues
- Reduce lock contention with lock-free data structures
- Set CPU affinity for worker threads

### 4. Network Stack Tuning
- Implement multiple EventLoops for connection distribution
- Add backpressure handling for slow clients
- Implement message compression for large payloads

### 5. Additional Enhancements
- Implement batching for database and Redis operations
- Add load shedding mechanisms for extreme conditions
- Implement monitoring and metrics collection
- Add pre-allocated data structures for common operations

## Implementation Timeline

### Phase 1: Foundation (Weeks 1-4)
- Set up development environment
- Implement Protobuf serialization
- Create Kafka integration
- Begin async logging implementation

### Phase 2: Database and Caching (Weeks 5-8)
- Implement Redis caching layer
- Optimize MySQL database architecture
- Set up master-slave replication
- Implement connection pooling

### Phase 3: Infrastructure (Weeks 9-12)
- Configure Nginx load balancing
- Complete async logging system
- Implement additional performance optimizations
- Begin integration testing

### Phase 4: Testing and Deployment (Weeks 13-16)
- Performance testing and optimization
- Load testing with target specifications
- Gradual rollout to production
- Monitoring and fine-tuning

## Success Metrics
- Achieve 10K+ QPS in load testing
- Maintain 1000+ concurrent connections
- Process 1300+ private messages per second
- Response times under 10ms for cached data
- 99.9% uptime with proper failover

## Redis Cluster and High Availability Configuration (Added 2025-09-01)

### Redis Persistence Configuration
**Objective**: Ensure Redis data is persisted to disk to prevent data loss on restarts.

#### Implementation:
- Configure RDB (snapshot) persistence with appropriate save intervals
- Configure AOF (Append-Only File) persistence for better durability
- Set up proper backup strategies for Redis data files
- Implement automatic backup rotation

#### Configuration Example:
```redis
# RDB Persistence
save 900 1
save 300 10
save 60 10000

# AOF Persistence
appendonly yes
appendfilename "appendonly.aof"
appendfsync everysec
auto-aof-rewrite-percentage 100
auto-aof-rewrite-min-size 64mb
```

### Redis Master-Slave Replication
**Objective**: Set up Redis replication for high availability and read scaling.

#### Implementation:
- Configure one Redis instance as master
- Configure multiple Redis instances as slaves
- Set up proper replication configuration
- Implement automatic failover mechanisms

#### Configuration Example:
```redis
# Master Configuration
port 6379
daemonize yes
pidfile /var/run/redis_6379.pid

# Slave Configuration
port 6380
daemonize yes
pidfile /var/run/redis_6380.pid
replicaof 127.0.0.1 6379
```

### Redis Sentinel for High Availability
**Objective**: Implement Redis Sentinel for automatic failover and monitoring.

#### Implementation:
- Deploy Redis Sentinel instances (minimum 3 for quorum)
- Configure Sentinel monitoring of master and slaves
- Set up automatic failover policies
- Implement proper notification mechanisms

#### Configuration Example:
```redis
# sentinel.conf
port 26379
sentinel monitor mymaster 127.0.0.1 6379 2
sentinel down-after-milliseconds mymaster 5000
sentinel failover-timeout mymaster 10000
sentinel parallel-syncs mymaster 1
```

### Redis Cluster Configuration
**Objective**: Implement Redis Cluster for horizontal scaling and data sharding.

#### Implementation:
- Deploy 6 or more Redis instances for cluster (minimum 3 master + 3 slave)
- Configure cluster-enabled mode
- Set up proper hash slot distribution
- Implement cluster-aware client connections

#### Configuration Example:
```redis
# redis.conf for cluster
port 7000
cluster-enabled yes
cluster-config-file nodes.conf
cluster-node-timeout 5000
appendonly yes
```

### Docker Compose Configuration for Redis HA
**Objective**: Update Docker Compose to support Redis high availability setup.

#### Implementation:
- Add Redis Sentinel services to docker-compose.yml
- Add multiple Redis instances for clustering
- Configure proper networking for Redis instances
- Set up volume persistence for all Redis instances

#### Configuration Example:
```yaml
services:
  redis-master:
    image: redis:7-alpine
    command: redis-server --appendonly yes
    ports:
      - "6379:6379"
    volumes:
      - redis_master_data:/data

  redis-slave1:
    image: redis:7-alpine
    command: redis-server --appendonly yes --replicaof redis-master 6379
    ports:
      - "6380:6379"
    volumes:
      - redis_slave1_data:/data
    depends_on:
      - redis-master

  redis-slave2:
    image: redis:7-alpine
    command: redis-server --appendonly yes --replicaof redis-master 6379
    ports:
      - "6381:6379"
    volumes:
      - redis_slave2_data:/data
    depends_on:
      - redis-master

  redis-sentinel1:
    image: redis:7-alpine
    command: redis-sentinel /etc/redis/sentinel.conf
    ports:
      - "26379:26379"
    volumes:
      - ./redis-sentinel.conf:/etc/redis/sentinel.conf
    depends_on:
      - redis-master

volumes:
  redis_master_data:
  redis_slave1_data:
  redis_slave2_data:
```

### Application Code Updates for Redis HA
**Objective**: Update application code to work with Redis high availability setup.

#### Implementation:
- Modify Redis connection logic to support Sentinel
- Update cache manager to handle failover scenarios
- Implement retry mechanisms for Redis operations
- Add proper error handling for cluster operations

#### Code Example:
```cpp
// Updated RedisCache class to support Sentinel
class RedisCache {
public:
    // Connect using Sentinel
    bool connectWithSentinel(const std::vector<std::string>& sentinelAddrs);
    
    // Handle failover
    bool handleFailover();
    
    // Retry mechanism
    bool executeWithRetry(const std::function<bool()>& operation, int maxRetries = 3);
};
```

## Kafka Integration Completion (Added 2025-09-01)

### Message Queue Migration Status
**Status**: COMPLETE - Redis pub/sub has been fully replaced with Kafka

#### Implementation Details:
- KafkaProducer and KafkaConsumer classes fully implemented
- KafkaManager class for connection management
- All chat service message routing updated to use Kafka
- Redis pub/sub completely removed from message handling logic
- Backward compatibility maintained during transition

#### Benefits Achieved:
- Message persistence with disk storage
- Horizontal scalability with multiple Kafka brokers
- Guaranteed message ordering
- Improved fault tolerance with replication

## Monitoring and Metrics for Redis HA (Added 2025-09-01)

### Key Metrics to Monitor
- Redis memory usage and fragmentation
- Persistence performance (RDB/AOF)
- Replication lag between master and slaves
- Sentinel failover events
- Cluster node health status
- Connection pool utilization

### Health Checks
- Regular Redis instance pings
- Replication status verification
- Sentinel quorum checks
- Cluster node availability monitoring
- Performance metrics collection

###
文档总体概览
这份文档的核心目标是：通过MySQL主从复制技术，将一个聊天应用从单数据库架构升级为支持读写分离的高可用、高性能架构。

核心思想（比喻）：
想象一个图书馆：

单数据库：就像只有一个图书管理员。所有人（借书、还书、查询）都要找他，他很容易成为瓶颈。

主从复制：就像有一个总管理员（Master） 和多个助理管理员（Slaves）。

写操作（借书、还书）：必须找总管理员登记，他会把登记的内容抄录到一个小本本（二进制日志）上。

读操作（查询书在哪）：可以找任何一个助理管理员。他们会定期从总管理员的小本本上抄写最新的记录，所以也能告诉你准确的信息。

好处：总管理员压力小了，整个图书馆的服务能力和效率都提高了。即使总管理员临时有事，助理管理员也能继续提供查询服务。

第一部分：实施计划 (Implementation Plan)
这是一个分三步走的稳健策略，避免一次性改动太大带来的风险。

Phase 1: 基础设施搭建

做什么：先在物理上或通过Docker搭建好主库和从库服务器，并配置好它们之间的复制关系。

为什么重要：这是地基。如果复制本身没配好，后面的代码写得再好也没用。这一步是纯DBA/运维工作。

Phase 2: 代码修改

做什么：修改应用程序的代码，让它知道现在有多个数据库，并且懂得区分"读"和"写"操作，将请求发往正确的数据库。

为什么重要：这是大脑。应用程序需要变得智能，这是本次升级的核心开发工作。

Phase 3: 测试与部署

做什么：全面测试新架构的功能、性能和稳定性，然后逐步上线。

为什么重要：这是安全网。没有经过充分测试就上线是危险的。逐步上线（如先上 staging 环境，再上生产环境的一部分流量）可以最小化风险。

第二部分：数据库类修改 (Database Class Modifications)
这是代码改造的核心，主要改造两个类：MySQL 和 ConnectionPool。

2.1 MySQL 类增强
原来的 MySQL 类只管理一个数据库连接。现在需要让它知道它连的是"主"还是"从"。

enum DBRole { MASTER, SLAVE };

作用：定义一个枚举类型，给每个连接打个"标签"，标明它的身份是主库连接还是从库连接。

为什么需要：这是实现路由的基础。连接池在分配连接时需要知道这个连接是通向主库的还是通向从库的。

MySQL(DBRole role = MASTER);

作用：构造函数现在可以接受一个参数，指定要创建的是主库连接还是从库连接。

默认值：MASTER 是默认值，这是为了保持向后兼容。如果旧代码没改，它默认创建的还是主库连接，不会立刻报错。

MYSQL_RES *query(std::string sql, bool useMaster = false);

作用：查询方法增加了一个参数 useMaster。

为什么需要：读写分离不是绝对的。有时候"读"操作也必须从主库读。最典型的场景是：用户刚注册完（写主库），立刻登录。如果登录的查询请求发到了从库，而从库复制有延迟，还没同步到新用户的数据，登录就会失败。此时就需要强制这个"读"操作去主库执行。useMaster = true 就是这个开关。

2.2 ConnectionPool 类增强
原来的连接池只有一个池子。现在需要有两个：一个主库连接池，一个（或多个）从库连接池。

initMaster 和 initSlaves

作用：分别初始化主库和从库的连接池。注意 initSlaves 的参数是 std::vector<std::string> servers，说明支持多个从库地址。

getMasterConnection() 和 getSlaveConnection()

作用：应用程序不再直接获取一个通用连接，而是明确地告诉连接池："我要一个用于写的连接"或"我要一个用于读的连接"。

成员变量：两个池子

masterConnections_：一个队列，专门存放空闲的、连接到主库的 MySQL 对象。

slaveConnections_：一个向量的队列 vector<queue<...>>。这是为了支持多个从库。

例如：slaveConnections_[0] 是连接到 slaveServers_[0]（如 192.168.1.11）的所有空闲连接。

slaveConnections_[1] 是连接到 slaveServers_[1]（如 192.168.1.12）的所有空闲连接。

currentSlaveIndex_：一个原子计数器，用于实现轮询负载均衡。当有请求要获取从库连接时，就按顺序 (0, 1, 2, ...) 从不同的从库连接池里取，让每个从库的读请求负载大致均衡。

void healthCheck();

作用：定期检查池中连接是否还有效（例如，数据库重启了，连接就失效了）。

为什么需要：连接池里的连接是长连接，可能存活很久。网络波动或数据库维护都可能导致连接断开。健康检查能及时清理掉无效连接，保证应用程序拿到的连接都是可用的。

第三部分：读写分离实现 (Read/Write Splitting Implementation)
这是给应用程序开发者用的智能路由层，它封装了底层复杂的连接池，提供了非常简单的接口。

3.1 DatabaseRouter 类
routeUpdate()

作用：帮我拿一个用于写操作的连接（即从主库连接池拿）。

用法：在执行 INSERT, UPDATE, DELETE 语句前调用。

routeQuery(bool preferMaster = false)

作用：帮我拿一个用于读操作的连接。默认从从库池里轮询获取（preferMaster = false）。如果设置了 preferMaster = true，则强制从主库读。

用法：在执行 SELECT 语句前调用。99%的情况用默认值，只在需要强一致性读时（如刚写完就读）才传 true。

3.2 Model 类修改
这是最终要改动业务代码的地方。所有操作数据库的类（如 UserModel）都需要修改。

修改前（旧代码）：

cpp
bool UserModel::insert(User& user) {
    // 1. 原始写法：直接从单一的连接池获取连接，不知道主从
    auto conn = _pool->getConnection();
    // 2. 执行insert...
    // 3. 归还连接
    _pool->releaseConnection(conn);
}
修改后（新代码）：

cpp
bool UserModel::insert(User& user) {
    // 1. 使用路由器：明确告诉路由器我要进行"更新"操作
    auto conn = DatabaseRouter::instance()->routeUpdate(); // 拿主库连接
    // 2. 执行insert...
    // 3. 归还连接
    DatabaseRouter::instance()->returnConnection(conn);
}

User UserModel::query(int id) {
    // 1. 使用路由器：默认进行"查询"操作（去从库）
    auto conn = DatabaseRouter::instance()->routeQuery(); // 拿从库连接
    // 2. 执行select...
    // 3. 归还连接
    DatabaseRouter::instance()->returnConnection(conn);
}
关键进步：业务代码不再关心数据库架构的细节，它只声明自己的意图（我要读还是写），路由的细节被完美地封装了起来。

第四 & 五部分：连接管理与故障转移 (Connection Management & Failover Handling)
这部分是关于鲁棒性的设计，确保系统在出现问题时也能保持稳定或快速恢复。

连接池大小策略：

masterMaxSize_（主池）设置得较小，因为写操作通常远少于读操作。

slaveMaxSize_（从池）设置得较大，以应对大量的读请求。

健康检查：

有一个后台线程定期（比如每分钟）对池中的所有连接执行 mysql_ping()。

如果ping不通，就将这个无效连接丢弃，并尝试创建一个新的来补充池子。

如果整个从库服务器都ping不通，就把整个从库对应的连接池标记为不可用，不再向其分配请求。

故障转移：

从库故障：相对简单。健康检查发现后，自动从负载均衡池中移除它即可。等它恢复了再加回来。

主库故障：非常严重！需要人工介入或借助专业的中间件（如Orchestrator, MHA）。

步骤：① 确认主库宕机 -> ② 选择一个数据最最新的从库提升为新主库 -> ③ 让其他从库改为从新主库复制 -> ④ 修改应用程序的配置，将 master_server 的地址指向新的主库。

文档中提到的"自动更新应用配置"在实际中非常复杂，通常需要配合配置中心（如Consul, Etcd）或代理（如ProxySQL）来实现。

第六部分：配置更改 (Configuration Changes)
代码变智能了，配置也要跟上。

核心变化：配置项从原来的一个 mysql_server，变成了一个 master_server 和一个列表 slave_servers。

Docker配置：docker-compose.yml 文件清晰地展示了如何在一个开发环境中启动 1个主库 + 2个从库，并映射不同的端口（3306, 3307, 3308）到宿主机。

第七、八部分：测试、监控与安全 (Testing, Monitoring & Security)
这是保证项目成功的"三条安全带"。

测试：

单元测试：测试代码逻辑对不对（如：routeUpdate() 返回的是不是主库连接？）。

集成测试：测试整个流程通不通（如：在主库插入，从从库能查到吗？复制延迟如何处理？）。

性能测试：测试效果好不好（如：引入读写分离后，QPS是否真的提高了？）。

负载测试：测试稳定性高不高（如：模拟1000个用户同时在线，系统会崩吗？）。

监控：

监控是运维的眼睛。必须要监控：

复制延迟：从库比主库慢了多少秒？延迟太大会导致用户读到旧数据。

连接池状态：主/从池还有多少空闲连接？是不是不够用了？

错误日志：有没有大量的连接错误？

安全：

加密：生产环境中，应用程序到数据库之间的连接应该使用SSL加密，防止数据被窃听。

权限：主从复制的账号（repl）和应用程序连接的账号（chatuser）应该分开，并且权限最小化。

第九部分：回滚计划 (Rollback Plan)
一个没有回滚计划的上线方案是极其危险的。 这份文档考虑了这一点，非常专业。

如何回滚：如果新版本发现问题，可以快速修改配置，让 DatabaseRouter 和 ConnectionPool 的所有读/写请求都指向原来的主库地址，瞬间降级回单库模式。

前提条件：在回滚前，必须确保所有从库都已经追上了主库的数据，保证数据一致性，否则回滚后会有数据丢失的风险。

总结
这份设计文档描绘了一个非常成熟、完整的数据库架构升级方案。它不仅仅是在技术上实现了主从复制，更是在架构设计、代码组织、自动化运维、安全保障和风险控制等方面都进行了充分的考虑。

核心价值在于：

性能提升：通过读写分离，大幅提升系统的并发处理能力。

高可用性：主库宕机后，从库可以继续提供读服务，并有机会晋升为新主库。

可扩展性：未来如果读压力持续增大，可以非常方便地添加新的从库来水平扩展。

透明化：对业务代码的侵入性较小，开发者几乎无需关心底层数据库的复杂性。