#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

/**
 * 单例模式：确保整个程序只有一个连接池实例。

主从分离：独立管理主库（Master）和从库（Slave）的连接池，支持读写分离架构。

线程安全：使用互斥锁（mutex）和条件变量（condition_variable）保证多线程并发获取和归还连接时的正确性。

动态扩容：当池中连接不够用时，可以创建新的连接（直到达到上限）。

连接健康检查：在将连接分配给用户前，检查它是否还有效（mysql_ping）。

负载均衡：对从库连接使用轮询（Round-Robin）策略进行分配。
 */


/*
*两个连接队列：一个用于主库（masterConnections_），一个用于从库（slaveConnections_，这是一个队列的向量，每个从库对应一个队列）。

同步机制：每个队列都有自己的锁（mutex）和条件变量（condition_variable）。

配置信息：数据库地址、用户名、密码等。

连接工厂方法：createConnection 用于创建新连接。
*/


#include "db.h"
#include <queue>// 使用队列（FIFO）来管理空闲连接
#include <mutex>// 互斥锁，用于线程同步
#include <condition_variable>// 条件变量，用于线程间通信（等待/通知）
#include <memory>
#include <string>
#include <vector>
#include <atomic>// 原子操作，用于无锁的轮询计数

// MySQL连接池类
class ConnectionPool {
public:

    // 获取连接池单例对象的静态方法。这是单例模式的经典实现方式。
    static ConnectionPool* instance();
    
    // 初始化连接池
    // 简化初始化（实为初始化主库）
    bool init(const std::string& server, const std::string& user, 
              const std::string& password, const std::string& dbname,
              int port, int maxSize);
    
    // 初始化主库连接池
    // 明确初始化主库连接池
    bool initMaster(const std::string& server, const std::string& user,
                    const std::string& password, const std::string& dbname,
                    int port, int maxSize);
    
    // 初始化从库连接池
    // 初始化所有从库的连接池
    bool initSlaves(const std::vector<std::string>& servers,
                    const std::string& user, const std::string& password,
                    const std::string& dbname, int port, int maxSize);
    
    // 获取一个数据库连接 // 默认获取主库连接
    std::shared_ptr<MySQL> getConnection();
    
    // 获取主库连接 // 明确获取主库连接
    std::shared_ptr<MySQL> getMasterConnection();
    
    // 获取从库连接 // 获取一个从库连接（轮询）
    std::shared_ptr<MySQL> getSlaveConnection();
    
    // 获取指定角色的连接 // 根据角色获取
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    
    // 归还连接到连接池
    void returnConnection(std::shared_ptr<MySQL> conn);
    
    // 获取连接池当前大小
    size_t size() const;
    
    // 获取主库连接池大小
    size_t masterSize() const;
    
    // 获取从库连接池大小
    size_t slaveSize() const;
    
    // 启动健康检查线程
    void startHealthCheck(int intervalSeconds = 30);
    
    // 停止健康检查线程
    void stopHealthCheck();
    
    // 执行一次健康检查
    void performHealthCheck();
    
    // 获取主库可用状态
    bool isMasterAvailable() const { return masterAvailable_; }
    
    // 获取指定从库可用状态
    bool isSlaveAvailable(size_t index) const {
        if (index < slaveAvailable_.size()) {
            return slaveAvailable_[index];
        }
        return false;
    }
    
    // 获取可用从库数量
    size_t getAvailableSlaveCount() const {
        size_t count = 0;
        for (const auto& available : slaveAvailable_) {
            if (available) count++;
        }
        return count;
    }
    
private:

    // 构造函数和析构函数私有化，防止外部创建和销毁实例，这是单例模式的关键。    
    ConnectionPool();
    ~ConnectionPool();
    
    // 创建新的数据库连接 用于创建新的连接对象
    std::shared_ptr<MySQL> createConnection();
    
    // 创建指定角色的数据库连接
    std::shared_ptr<MySQL> createConnection(MySQL::DBRole role, const std::string& server);
    
    // 主库连接池 相关成员
    std::string masterServer_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int port_;
    int masterMaxSize_;
    mutable std::mutex masterMutex_; // 保护主池队列的锁
    std::condition_variable masterCondition_; // 主池的条件变量（用于等待可用连接）
    std::queue<std::shared_ptr<MySQL>> masterConnections_; // 主库空闲连接队列
    
    // 从库连接池 相关成员
    std::vector<std::string> slaveServers_; // 所有从库地址列表
    int slaveMaxSize_; // 每个从库连接池的最大连接数
    mutable std::mutex slaveMutex_;  // 保护所有从库池队列的锁（这里用了一把大锁）
    std::condition_variable slaveCondition_;  // 从库池的条件变量
    std::vector<std::queue<std::shared_ptr<MySQL>>> slaveConnections_; // 每个从库对应一个空闲连接队列
    std::atomic<size_t> currentSlaveIndex_;// 原子计数器，用于实现轮询算法
    
    // 健康检查相关
    std::atomic<bool> masterAvailable_;  // 主库是否可用
    std::vector<std::atomic<bool>> slaveAvailable_;  // 每个从库是否可用
    int healthCheckInterval_;  // 健康检查间隔（秒）
    std::thread healthCheckThread_;  // 健康检查线程
    bool running_;  // 运行标志
};

#endif