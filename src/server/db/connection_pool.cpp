#include "connection_pool.h"
#include <iostream>
#include <muduo/base/Logging.h>
#include <thread>


ConnectionPool::ConnectionPool() : port_(3306), masterMaxSize_(10), slaveMaxSize_(10), currentSlaveIndex_(0),
    masterAvailable_(true), healthCheckInterval_(30) {
    slaveAvailable_ = std::make_unique<std::atomic<bool>[]>(1);
    slaveAvailable_[0] = true;
}

ConnectionPool::~ConnectionPool() {
    while (!masterConnections_.empty()) {
        masterConnections_.pop();
    }
    for (auto& slaveQueue : slaveConnections_) {
        while (!slaveQueue.empty()) {
            slaveQueue.pop();
        }
    }
}

ConnectionPool* ConnectionPool::instance() {
    static ConnectionPool pool;
    return &pool;
}

bool ConnectionPool::init(const std::string& server, const std::string& user, 
                          const std::string& password, const std::string& dbname,
                          int port, int maxSize) {
    return initMaster(server, user, password, dbname, port, maxSize);
}

bool ConnectionPool::initMaster(const std::string& server, const std::string& user,
                                const std::string& password, const std::string& dbname,
                                int port, int maxSize) {
    masterServer_ = server;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    port_ = port;
    masterMaxSize_ = maxSize;
    
    for (int i = 0; i < masterMaxSize_ / 2; ++i) {
        auto conn = createConnection(MySQL::MASTER, masterServer_);
        if (conn) {
            masterConnections_.push(conn); masterTotalCount_++;
        }
    }
    
    LOG_INFO << "Master connection pool initialized with " << masterConnections_.size() << " connections";
    return !masterConnections_.empty();
}



bool ConnectionPool::initSlaves(const std::vector<std::string>& servers,
                                const std::string& user, const std::string& password,
                                const std::string& dbname, int port, int maxSize) {

    slaveServers_ = servers;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    port_ = port;
    slaveMaxSize_ = maxSize;
    
    slaveConnections_.resize(slaveServers_.size());
    slaveTotalCounts_ = std::make_unique<std::atomic<int>[]>(slaveServers_.size());
    for(size_t i=0; i<slaveServers_.size(); ++i) slaveTotalCounts_[i].store(0);
    slaveAvailable_ = std::make_unique<std::atomic<bool>[]>(slaveServers_.size());
    for (size_t i = 0; i < slaveServers_.size(); ++i) {
        slaveAvailable_[i].store(true);
    }
    
    for (size_t i = 0; i < slaveServers_.size(); ++i) {
        for (int j = 0; j < slaveMaxSize_ / 2; ++j) {
            auto conn = createConnection(MySQL::SLAVE, slaveServers_[i]);
            if (conn) {
                slaveConnections_[i].push(conn); slaveTotalCounts_[i]++;
            }
        }
        if (slaveConnections_[i].empty()) {
            slaveAvailable_[i].store(false);
            LOG_ERROR << "Slave " << i << " (" << slaveServers_[i]
                      << ") initialization failed - no connections created, marking unavailable";
        } else {
            LOG_INFO << "Slave " << i << " connection pool initialized with " 
                     << slaveConnections_[i].size() << " connections";
        }
    }
    
    return true;
}


std::shared_ptr<MySQL> ConnectionPool::createConnection() {
    // 默认创建主库连接
    return createConnection(MySQL::MASTER, masterServer_);
}


std::shared_ptr<MySQL> ConnectionPool::createConnection(MySQL::DBRole role, const std::string& server) {
    std::shared_ptr<MySQL> conn(new MySQL(role));

    std::string host = server;
    int port = port_;
    auto colonPos = server.find(':');
    if (colonPos != std::string::npos) {
        host = server.substr(0, colonPos);
        port = std::stoi(server.substr(colonPos + 1));
    }
    
    if (conn->connect(host, user_, password_, dbname_, port)) {
        return conn;
    }
    return nullptr;
}

std::shared_ptr<MySQL> ConnectionPool::getConnection() {
    // 默认获取主库连接
    return getMasterConnection();
}

std::shared_ptr<MySQL> ConnectionPool::getMasterConnection() {
    if (!masterAvailable_) {
        LOG_ERROR << "Master database is unavailable!";
        if (!slaveServers_.empty()) {
            LOG_WARN << "Falling back to slave for write operation";
            return getSlaveConnection();
        }
    }

    std::unique_lock<std::mutex> lock(masterMutex_);

    if (masterConnections_.empty() && masterTotalCount_ < masterMaxSize_) {
        auto conn = createConnection(MySQL::MASTER, masterServer_);
        if (conn) {
            masterAvailable_ = true;
            masterTotalCount_++;
            return conn;
        } else {
            masterAvailable_ = false;
        }
    }

    while (masterConnections_.empty()) {
        masterCondition_.wait(lock);
    }
    auto conn = masterConnections_.front();
    masterConnections_.pop();

    if (conn && mysql_ping(conn->getConnection()) != 0) {
        conn = createConnection(MySQL::MASTER, masterServer_);
        if (!conn) {
            masterAvailable_ = false;
        }
    } else {
        masterAvailable_ = true;
    }

    return conn;
}

std::shared_ptr<MySQL> ConnectionPool::getSlaveConnection() {
    if (slaveServers_.empty()) {
        return getMasterConnection();
    }

    const int maxRetries = 3;
    int retryCount = 0;

    while (retryCount < maxRetries) {
        size_t slaveIndex = currentSlaveIndex_++ % slaveServers_.size();

        if (!slaveAvailable_[slaveIndex]) {
            retryCount++;
            LOG_WARN << "Slave " << slaveIndex << " is unavailable, trying next...";
            continue;
        }

        {
            std::unique_lock<std::mutex> lock(slaveMutex_);

            if (slaveConnections_[slaveIndex].empty() && slaveTotalCounts_[slaveIndex] < slaveMaxSize_) {
                auto conn = createConnection(MySQL::SLAVE, slaveServers_[slaveIndex]);
                if (conn) {
                    slaveTotalCounts_[slaveIndex]++;
                    return conn;
                }
                slaveAvailable_[slaveIndex] = false;
                LOG_ERROR << "Failed to create connection to slave " << slaveIndex
                          << " (" << slaveServers_[slaveIndex] << "), marking unavailable";
                retryCount++;
                continue;
            }

            while (slaveConnections_[slaveIndex].empty()) {
                if (slaveCondition_.wait_for(lock, std::chrono::seconds(5))
                    == std::cv_status::timeout) {
                    slaveAvailable_[slaveIndex] = false;
                    LOG_ERROR << "Timeout waiting for slave " << slaveIndex
                              << " connection, marking unavailable";
                    retryCount++;
                    break;
                }
            }
            if (slaveConnections_[slaveIndex].empty()) {
                continue;
            }

            auto conn = slaveConnections_[slaveIndex].front();
            slaveConnections_[slaveIndex].pop();

            if (conn && mysql_ping(conn->getConnection()) != 0) {
                conn = createConnection(MySQL::SLAVE, slaveServers_[slaveIndex]);
                if (!conn) {
                    slaveAvailable_[slaveIndex] = false;
                    LOG_ERROR << "Failed to create connection to slave " << slaveIndex
                              << " (" << slaveServers_[slaveIndex] << "), marking unavailable";
                    retryCount++;
                    continue;
                }
            }

            return conn;
        }
    }

    LOG_WARN << "All slaves unavailable, falling back to master";
    return getMasterConnection();
}

std::shared_ptr<MySQL> ConnectionPool::getConnection(MySQL::DBRole role) {
    if (role == MySQL::MASTER) {
        return getMasterConnection();
    } else {
        return getSlaveConnection();
    }
}

void ConnectionPool::returnConnection(std::shared_ptr<MySQL> conn) {
    if (!conn) return;

    if (conn->getRole() == MySQL::MASTER) {
        std::lock_guard<std::mutex> lock(masterMutex_);
        masterConnections_.push(conn);
        masterCondition_.notify_one();
    } else {
        std::lock_guard<std::mutex> lock(slaveMutex_);
        const std::string& server = conn->getServer();
        bool placed = false;
        for (size_t i = 0; i < slaveServers_.size(); ++i) {
            if (slaveServers_[i] == server) {
                slaveConnections_[i].push(conn);
                placed = true;
                break;
            }
        }
        if (!placed) {
            size_t idx = currentSlaveIndex_ % (slaveConnections_.empty() ? 1 : slaveConnections_.size());
            if (!slaveConnections_.empty()) {
                slaveConnections_[idx].push(conn);
            }
        }
        slaveCondition_.notify_one();
    }
}

size_t ConnectionPool::size() const {
    return masterSize() + slaveSize();
}

size_t ConnectionPool::masterSize() const {
    std::lock_guard<std::mutex> lock(masterMutex_);
    return masterConnections_.size();
}

size_t ConnectionPool::slaveSize() const {
    std::lock_guard<std::mutex> lock(slaveMutex_);
    size_t total = 0;
    for (const auto& queue : slaveConnections_) {
        total += queue.size();
    }
    return total;
}

void ConnectionPool::startHealthCheck(int intervalSeconds) {
    healthCheckInterval_ = intervalSeconds;
    running_ = true;
    healthCheckThread_ = std::thread([this]() {
        try {
            while (running_) {
                std::this_thread::sleep_for(std::chrono::seconds(healthCheckInterval_));
                if (!running_) break;
                performHealthCheck();
            }
        } catch (const std::exception& e) {
            std::cerr << "Health check thread exception: " << e.what() << std::endl;
        }
    });
    LOG_INFO << "Health check thread started, interval: " << healthCheckInterval_ << "s";
}

void ConnectionPool::stopHealthCheck() {
    running_ = false;
    if (healthCheckThread_.joinable()) {
        healthCheckThread_.join();
    }
    LOG_INFO << "Health check thread stopped";
}

void ConnectionPool::performHealthCheck() {
    // 检查主库
    {
        std::lock_guard<std::mutex> lock(masterMutex_);
        if (!masterConnections_.empty()) {
            auto conn = masterConnections_.front();
            if (conn && mysql_ping(conn->getConnection()) != 0) {
                if (masterAvailable_) {
                    masterAvailable_ = false;
                    LOG_ERROR << "Health check: Master database unavailable!";
                }
            } else {
                if (!masterAvailable_) {
                    masterAvailable_ = true;
                    LOG_INFO << "Health check: Master database recovered!";
                }
            }
        }
    }
    
    // 检查从库
    std::lock_guard<std::mutex> lock(slaveMutex_);
    for (size_t i = 0; i < slaveConnections_.size(); ++i) {
        if (!slaveConnections_[i].empty()) {
            auto conn = slaveConnections_[i].front();
            if (conn && mysql_ping(conn->getConnection()) != 0) {
                if (slaveAvailable_[i]) {
                    slaveAvailable_[i] = false;
                    LOG_ERROR << "Health check: Slave " << i << " unavailable!";
                }
            } else {
                if (!slaveAvailable_[i]) {
                    slaveAvailable_[i] = true;
                    LOG_INFO << "Health check: Slave " << i << " recovered!";
                }
            }
        }
    }
}