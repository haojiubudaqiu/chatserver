# MySQL Master-Slave Replication Upgrade Design Document

## Overview

This document outlines the design for implementing MySQL master-slave replication in the chat server to improve performance, availability, and scalability. The implementation will focus on read/write splitting, connection management, and failover handling.

## 1. Implementation Plan

### 1.1 Phase 1: Infrastructure Setup
1. Set up MySQL master database server
2. Set up MySQL slave database server(s)
3. Configure master-slave replication between the servers
4. Verify replication is working correctly

### 1.2 Phase 2: Code Modifications
1. Modify existing database classes to support multiple database connections
2. Implement read/write splitting logic
3. Create separate connection pools for master and slave databases
4. Add failover handling mechanisms

### 1.3 Phase 3: Testing and Deployment
1. Test replication and failover functionality
2. Perform load testing to verify performance improvements
3. Deploy to staging environment
4. Gradual rollout to production

## 2. Database Class Modifications

### 2.1 MySQL Class Enhancement
The existing `MySQL` class in `include/server/db/db.h` and `src/server/db/db.cpp` needs to be enhanced to support master-slave replication:

#### New Features:
- Support for multiple database connections (master and slaves)
- Read/write operation routing
- Connection health checking
- Automatic failover capabilities

#### Modified Class Structure:
```cpp
class MySQL {
public:
    enum DBRole {
        MASTER,
        SLAVE
    };
    
    // Initialize database connection with role
    MySQL(DBRole role = MASTER);
    
    // Connect to database with specific role
    bool connect(const std::string& server, const std::string& user,
                 const std::string& password, const std::string& dbname,
                 int port = 3306);
    
    // Update operations (always go to master)
    bool update(std::string sql);
    
    // Query operations (can go to slaves)
    MYSQL_RES *query(std::string sql, bool useMaster = false);
    
    // Connection health check
    bool ping();
    
    // Get connection role
    DBRole getRole() const;
    
    // Get connection
    MYSQL* getConnection();

private:
    MYSQL *_conn;
    DBRole _role;
    static bool useConnectionPool;
};
```

### 2.2 ConnectionPool Class Enhancement
The `ConnectionPool` class in `include/server/db/connection_pool.h` and `src/server/db/connection_pool.cpp` needs significant modifications:

#### New Features:
- Separate pools for master and slave connections
- Load balancing among slave connections
- Health checking for all connections
- Dynamic connection management

#### Modified Class Structure:
```cpp
class ConnectionPool {
public:
    static ConnectionPool* instance();
    
    // Initialize connection pools for master and slaves
    bool initMaster(const std::string& server, const std::string& user,
                    const std::string& password, const std::string& dbname,
                    int port, int maxSize);
    
    bool initSlaves(const std::vector<std::string>& servers,
                    const std::string& user, const std::string& password,
                    const std::string& dbname, int port, int maxSize);
    
    // Get master connection (for writes)
    std::shared_ptr<MySQL> getMasterConnection();
    
    // Get slave connection (for reads)
    std::shared_ptr<MySQL> getSlaveConnection();
    
    // Get connection based on operation type
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    
    // Return connection to appropriate pool
    void returnConnection(std::shared_ptr<MySQL> conn);
    
    // Get pool sizes
    size_t masterSize() const;
    size_t slaveSize() const;
    
    // Health check all connections
    void healthCheck();

private:
    ConnectionPool();
    ~ConnectionPool();
    
    // Create new connection with role
    std::shared_ptr<MySQL> createConnection(MySQL::DBRole role,
                                          const std::string& server);
    
    // Master connection pool
    std::string masterServer_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int port_;
    int masterMaxSize_;
    mutable std::mutex masterMutex_;
    std::condition_variable masterCondition_;
    std::queue<std::shared_ptr<MySQL>> masterConnections_;
    
    // Slave connection pools
    std::vector<std::string> slaveServers_;
    int slaveMaxSize_;
    mutable std::mutex slaveMutex_;
    std::condition_variable slaveCondition_;
    std::vector<std::queue<std::shared_ptr<MySQL>>> slaveConnections_;
    std::atomic<size_t> currentSlaveIndex_;
    
    // Health check
    std::atomic<bool> healthCheckRunning_;
};
```

## 3. Read/Write Splitting Implementation

### 3.1 Database Router Component
A new `DatabaseRouter` class will be created to handle automatic routing of queries:

```cpp
class DatabaseRouter {
public:
    static DatabaseRouter* instance();
    
    // Route update operations to master
    std::shared_ptr<MySQL> routeUpdate();
    
    // Route query operations to slaves (with option to use master)
    std::shared_ptr<MySQL> routeQuery(bool preferMaster = false);
    
    // Manual routing by role
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    
    // Return connection
    void returnConnection(std::shared_ptr<MySQL> conn);

private:
    DatabaseRouter();
    ~DatabaseRouter();
};
```

### 3.2 Model Class Modifications
All model classes (`UserModel`, `FriendModel`, `GroupModel`, etc.) will be updated to use the new routing mechanism:

#### Read Operations:
```cpp
// Example in UserModel
User UserModel::query(int id) {
    // Route to slave for read operations
    auto conn = DatabaseRouter::instance()->routeQuery();
    // ... perform query
    DatabaseRouter::instance()->returnConnection(conn);
}
```

#### Write Operations:
```cpp
// Example in UserModel
bool UserModel::insert(User& user) {
    // Route to master for write operations
    auto conn = DatabaseRouter::instance()->routeUpdate();
    // ... perform insert
    DatabaseRouter::instance()->returnConnection(conn);
}
```

## 4. Connection Management

### 4.1 Master Connection Pool
- Dedicated pool for write operations
- Smaller pool size as writes are less frequent
- Strict health checking
- Automatic failover handling

### 4.2 Slave Connection Pool
- Dedicated pools for read operations
- Larger pool size to handle read load
- Round-robin load balancing among slaves
- Health checking with automatic removal of failed slaves

### 4.3 Health Checking
- Periodic ping operations to verify connection status
- Automatic removal of failed connections
- Automatic reconnection attempts
- Monitoring and alerting for connection issues

## 5. Failover Handling

### 5.1 Master Failover
1. Detection of master failure through health checks
2. Promotion of a slave to master role
3. Reconfiguration of remaining slaves to replicate from new master
4. Update of application configuration to point to new master

### 5.2 Slave Failover
1. Detection of slave failure through health checks
2. Automatic removal of failed slave from connection pool
3. Redistribution of read load among remaining slaves
4. Attempt to reconnect to failed slave

### 5.3 Manual Failover
1. Administrative interface for manual failover
2. Controlled switchover with minimal downtime
3. Verification of data consistency before failover

## 6. Configuration Changes

### 6.1 New Configuration Parameters
Add the following configuration options to support master-slave replication:

```cpp
// Database configuration
static std::string master_server = "127.0.0.1";
static int master_port = 3306;
static std::string slave_servers = "127.0.0.1:3307,127.0.0.1:3308";
static int slave_port = 3306;
static std::string db_user = "root";
static std::string db_password = "password";
static std::string db_name = "chat";
```

### 6.2 Docker Configuration
Update `docker-compose.yml` to include multiple MySQL services:

```yaml
services:
  mysql-master:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: password
      MYSQL_DATABASE: chat
    volumes:
      - ./mysql-master:/var/lib/mysql
      - ./mysql-master.conf:/etc/mysql/conf.d/master.conf
    ports:
      - "3306:3306"
  
  mysql-slave1:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: password
      MYSQL_DATABASE: chat
    volumes:
      - ./mysql-slave1:/var/lib/mysql
      - ./mysql-slave.conf:/etc/mysql/conf.d/slave.conf
    ports:
      - "3307:3306"
    depends_on:
      - mysql-master
  
  mysql-slave2:
    image: mysql:8.0
    environment:
      MYSQL_ROOT_PASSWORD: password
      MYSQL_DATABASE: chat
    volumes:
      - ./mysql-slave2:/var/lib/mysql
      - ./mysql-slave.conf:/etc/mysql/conf.d/slave.conf
    ports:
      - "3308:3306"
    depends_on:
      - mysql-master
```

## 7. Testing Approach

### 7.1 Unit Tests
1. Test individual database operations with master/slave routing
2. Verify connection pool management
3. Test health checking functionality
4. Validate failover mechanisms

### 7.2 Integration Tests
1. Test end-to-end database operations
2. Verify read/write splitting is working correctly
3. Test replication consistency
4. Validate failover scenarios

### 7.3 Performance Testing
1. Measure query performance improvements with read distribution
2. Test system behavior under high load
3. Verify failover does not significantly impact performance
4. Monitor replication lag under load

### 7.4 Load Testing
1. Simulate concurrent read/write operations
2. Test connection pool behavior under stress
3. Verify system stability with multiple slaves
4. Measure throughput improvements

## 8. Monitoring and Metrics

### 8.1 Key Metrics to Monitor
1. Master and slave connection pool sizes
2. Query routing statistics (reads vs writes)
3. Replication lag times
4. Failover events
5. Database response times

### 8.2 Health Checks
1. Regular connection pings
2. Replication status verification
3. Disk space monitoring
4. Performance metrics collection

## 9. Security Considerations

### 9.1 Connection Security
1. Use encrypted connections between application and databases
2. Secure database user credentials
3. Implement proper access controls

### 9.2 Data Consistency
1. Ensure ACID compliance across replication
2. Handle replication lag in application logic
3. Implement proper error handling for consistency issues

## 10. Rollback Plan

### 10.1 Reverting to Single Database
1. Disable master-slave routing
2. Revert to original connection pool configuration
3. Update application to use single database connection
4. Verify system functionality

### 10.2 Data Consistency During Rollback
1. Ensure all slaves are synchronized with master
2. Handle any pending replication events
3. Verify data integrity before rollback

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

做什么：修改应用程序的代码，让它知道现在有多个数据库，并且懂得区分“读”和“写”操作，将请求发往正确的数据库。

为什么重要：这是大脑。应用程序需要变得智能，这是本次升级的核心开发工作。

Phase 3: 测试与部署

做什么：全面测试新架构的功能、性能和稳定性，然后逐步上线。

为什么重要：这是安全网。没有经过充分测试就上线是危险的。逐步上线（如先上 staging 环境，再上生产环境的一部分流量）可以最小化风险。

第二部分：数据库类修改 (Database Class Modifications)
这是代码改造的核心，主要改造两个类：MySQL 和 ConnectionPool。

2.1 MySQL 类增强
原来的 MySQL 类只管理一个数据库连接。现在需要让它知道它连的是“主”还是“从”。

enum DBRole { MASTER, SLAVE };

作用：定义一个枚举类型，给每个连接打个“标签”，标明它的身份是主库连接还是从库连接。

为什么需要：这是实现路由的基础。连接池在分配连接时需要知道这个连接是通向主库的还是通向从库的。

MySQL(DBRole role = MASTER);

作用：构造函数现在可以接受一个参数，指定要创建的是主库连接还是从库连接。

默认值：MASTER 是默认值，这是为了保持向后兼容。如果旧代码没改，它默认创建的还是主库连接，不会立刻报错。

MYSQL_RES *query(std::string sql, bool useMaster = false);

作用：查询方法增加了一个参数 useMaster。

为什么需要：读写分离不是绝对的。有时候“读”操作也必须从主库读。最典型的场景是：用户刚注册完（写主库），立刻登录。如果登录的查询请求发到了从库，而从库复制有延迟，还没同步到新用户的数据，登录就会失败。此时就需要强制这个“读”操作去主库执行。useMaster = true 就是这个开关。

2.2 ConnectionPool 类增强
原来的连接池只有一个池子。现在需要有两个：一个主库连接池，一个（或多个）从库连接池。

initMaster 和 initSlaves

作用：分别初始化主库和从库的连接池。注意 initSlaves 的参数是 std::vector<std::string> servers，说明支持多个从库地址。

getMasterConnection() 和 getSlaveConnection()

作用：应用程序不再直接获取一个通用连接，而是明确地告诉连接池：“我要一个用于写的连接”或“我要一个用于读的连接”。

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
    // 1. 使用路由器：明确告诉路由器我要进行“更新”操作
    auto conn = DatabaseRouter::instance()->routeUpdate(); // 拿主库连接
    // 2. 执行insert...
    // 3. 归还连接
    DatabaseRouter::instance()->returnConnection(conn);
}

User UserModel::query(int id) {
    // 1. 使用路由器：默认进行“查询”操作（去从库）
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

文档中提到的“自动更新应用配置”在实际中非常复杂，通常需要配合配置中心（如Consul, Etcd）或代理（如ProxySQL）来实现。

第六部分：配置更改 (Configuration Changes)
代码变智能了，配置也要跟上。

核心变化：配置项从原来的一个 mysql_server，变成了一个 master_server 和一个列表 slave_servers。

Docker配置：docker-compose.yml 文件清晰地展示了如何在一个开发环境中启动 1个主库 + 2个从库，并映射不同的端口（3306, 3307, 3308）到宿主机。

第七、八部分：测试、监控与安全 (Testing, Monitoring & Security)
这是保证项目成功的“三条安全带”。

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