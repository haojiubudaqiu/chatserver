#ifndef CONNECTION_POOL_H
#define CONNECTION_POOL_H

#include "db.h"
#include <queue>
#include <mutex>
#include <condition_variable>
#include <memory>
#include <string>
#include <vector>
#include <atomic>// 原子操作，用于无锁的轮询计数
#include <thread>

class ConnectionPool {
public:

    static ConnectionPool* instance();
    
    bool init(const std::string& server, const std::string& user, 
              const std::string& password, const std::string& dbname,
              int port, int maxSize);
    
    bool initMaster(const std::string& server, const std::string& user,
                    const std::string& password, const std::string& dbname,
                    int port, int maxSize);
    
    bool initSlaves(const std::vector<std::string>& servers,
                    const std::string& user, const std::string& password,
                    const std::string& dbname, int port, int maxSize);
    
    std::shared_ptr<MySQL> getConnection();
    std::shared_ptr<MySQL> getMasterConnection();
    std::shared_ptr<MySQL> getSlaveConnection();
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    void returnConnection(std::shared_ptr<MySQL> conn);
    size_t size() const;
    size_t masterSize() const;
    size_t slaveSize() const;
    void startHealthCheck(int intervalSeconds = 30);
    void stopHealthCheck();
    void performHealthCheck();

    bool isMasterAvailable() const { return masterAvailable_; }
    bool isSlaveAvailable(size_t index) const {
        if (index < slaveServers_.size()) {
            return slaveAvailable_[index];
        }
        return false;
    }
    
    size_t getAvailableSlaveCount() const {
        size_t count = 0;
        for (size_t i = 0; i < slaveServers_.size(); ++i) {
            if (slaveAvailable_[i]) count++;
        }
        return count;
    }
    
private:

    ConnectionPool();
    ~ConnectionPool();
    
    std::shared_ptr<MySQL> createConnection();
    std::shared_ptr<MySQL> createConnection(MySQL::DBRole role, const std::string& server);
    
    std::string masterServer_;
    std::string user_;
    std::string password_;
    std::string dbname_;
    int port_;
    int masterMaxSize_;
    mutable std::mutex masterMutex_;
    std::condition_variable masterCondition_;
    std::queue<std::shared_ptr<MySQL>> masterConnections_;
    
    std::vector<std::string> slaveServers_;
    int slaveMaxSize_;
    mutable std::mutex slaveMutex_;
    std::condition_variable slaveCondition_;
    std::vector<std::queue<std::shared_ptr<MySQL>>> slaveConnections_;
    std::atomic<size_t> currentSlaveIndex_;
    
    std::atomic<bool> masterAvailable_{true};
    std::unique_ptr<std::atomic<bool>[]> slaveAvailable_;
    std::thread healthCheckThread_;
    std::atomic<bool> running_{false};
    int healthCheckInterval_{30};

    std::atomic<int> masterTotalCount_{0};
    std::unique_ptr<std::atomic<int>[]> slaveTotalCounts_;
};

#endif