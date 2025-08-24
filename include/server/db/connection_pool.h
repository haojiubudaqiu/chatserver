#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include "db.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>

// MySQL连接池类
class ConnectionPool {
public:
    static ConnectionPool* instance();
    
    // 初始化连接池
    bool init(const std::string& server, const std::string& user, 
              const std::string& password, const std::string& dbname,
              int port, int maxSize);
    
    // 获取一个数据库连接
    std::shared_ptr<MySQL> getConnection();
    
    // 归还连接到连接池
    void returnConnection(std::shared_ptr<MySQL> conn);
    
    // 获取连接池当前大小
    size_t size() const;
    
private:
    ConnectionPool();
    ~ConnectionPool();
    
    // 创建新的数据库连接
    std::shared_ptr<MySQL> createConnection();
    
    std::string server_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int port_;
    int maxSize_;
    
    mutable std::mutex mutex_;
    std::condition_variable condition_;
    std::queue<std::shared_ptr<MySQL>> connections_;
};

#endif