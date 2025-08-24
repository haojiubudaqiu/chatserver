#include "connection_pool.h"
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>

ConnectionPool::ConnectionPool() : port_(3306), maxSize_(10) {}

ConnectionPool::~ConnectionPool() {
    // 清理所有连接
    while (!connections_.empty()) {
        connections_.pop();
    }
}

ConnectionPool* ConnectionPool::instance() {
    static ConnectionPool pool;
    return &pool;
}

bool ConnectionPool::init(const std::string& server, const std::string& user, 
                          const std::string& password, const std::string& dbname,
                          int port, int maxSize) {
    server_ = server;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    port_ = port;
    maxSize_ = maxSize;
    
    // 预先创建一些连接
    for (int i = 0; i < maxSize_ / 2; ++i) {
        auto conn = createConnection();
        if (conn) {
            connections_.push(conn);
        }
    }
    
    LOG_INFO << "Connection pool initialized with " << connections_.size() << " connections";
    return !connections_.empty();
}

std::shared_ptr<MySQL> ConnectionPool::createConnection() {
    std::shared_ptr<MySQL> conn(new MySQL());
    if (conn->connect()) {
        return conn;
    }
    return nullptr;
}

std::shared_ptr<MySQL> ConnectionPool::getConnection() {
    std::unique_lock<std::mutex> lock(mutex_);
    
    // 如果连接池为空且未达到最大连接数，创建新连接
    if (connections_.empty() && connections_.size() < static_cast<size_t>(maxSize_)) {
        auto conn = createConnection();
        if (conn) {
            return conn;
        }
    }
    
    // 等待可用连接
    while (connections_.empty()) {
        condition_.wait(lock);
    }
    
    auto conn = connections_.front();
    connections_.pop();
    
    // 检查连接是否仍然有效
    if (conn && mysql_ping(conn->getConnection()) != 0) {
        // 连接已断开，创建新连接
        conn = createConnection();
    }
    
    return conn;
}

void ConnectionPool::returnConnection(std::shared_ptr<MySQL> conn) {
    if (!conn) return;
    
    std::lock_guard<std::mutex> lock(mutex_);
    connections_.push(conn);
    condition_.notify_one();
}

size_t ConnectionPool::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return connections_.size();
}