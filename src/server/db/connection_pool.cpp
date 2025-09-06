#include "connection_pool.h"
#include <muduo/base/Logging.h>
#include <mysql/mysql.h>
#include <sstream>


 // 构造函数初始化列表：设置默认端口和最大连接数，将原子计数器归零。
ConnectionPool::ConnectionPool() : port_(3306), masterMaxSize_(10), slaveMaxSize_(10), currentSlaveIndex_(0) {}

ConnectionPool::~ConnectionPool() {
    // 清理主库连接
    // 析构函数：清空所有队列。注意：这里只是弹出智能指针，当智能指针对象被销毁时，
    // 会调用其析构函数，进而调用MySQL的析构函数关闭数据库连接。
    while (!masterConnections_.empty()) {
        masterConnections_.pop();
    }
    
    // 清理从库连接
    for (auto& slaveQueue : slaveConnections_) {
        while (!slaveQueue.empty()) {
            slaveQueue.pop();
        }
    }
}

//作用：获取连接池的全局唯一实例。
ConnectionPool* ConnectionPool::instance() {
    static ConnectionPool pool; // C++11保证局部静态变量初始化是线程安全的
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
    // 保存主库配置信息
    masterServer_ = server;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    port_ = port;
    masterMaxSize_ = maxSize;
    
    // 预先创建一些主库连接 避免系统刚启动时所有请求都等待创建新连接。
    for (int i = 0; i < masterMaxSize_ / 2; ++i) {
        auto conn = createConnection(MySQL::MASTER, masterServer_);
        if (conn) {
            masterConnections_.push(conn); // 创建成功，放入空闲队列
        }
    }
    
    LOG_INFO << "Master connection pool initialized with " << masterConnections_.size() << " connections";
    return !masterConnections_.empty();// 只要有一个连接创建成功，初始化就算成功
}



bool ConnectionPool::initSlaves(const std::vector<std::string>& servers,
                                const std::string& user, const std::string& password,
                                const std::string& dbname, int port, int maxSize) {

    // 保存从库配置
    slaveServers_ = servers;
    user_ = user;
    password_ = password;
    dbname_ = dbname;
    port_ = port;
    slaveMaxSize_ = maxSize;
    
    // 初始化从库连接队列 大小为从库服务器的数量
    slaveConnections_.resize(slaveServers_.size());
    
    // 为每个从库预先创建一些连接
    for (size_t i = 0; i < slaveServers_.size(); ++i) {
        for (int j = 0; j < slaveMaxSize_ / 2; ++j) {
            auto conn = createConnection(MySQL::SLAVE, slaveServers_[i]);
            if (conn) {
                slaveConnections_[i].push(conn);  // 将连接放入对应从库的队列
            }
        }
        LOG_INFO << "Slave " << i << " connection pool initialized with " 
                 << slaveConnections_[i].size() << " connections";
    }
    
    return true;
}


std::shared_ptr<MySQL> ConnectionPool::createConnection() {
    // 默认创建主库连接
    return createConnection(MySQL::MASTER, masterServer_);
}


// 连接工厂方法 
std::shared_ptr<MySQL> ConnectionPool::createConnection(MySQL::DBRole role, const std::string& server) {
    std::shared_ptr<MySQL> conn(new MySQL(role));
    if (conn->connect(server, user_, password_, dbname_, port_)) {
        return conn;
    }
    return nullptr;
}

std::shared_ptr<MySQL> ConnectionPool::getConnection() {
    // 默认获取主库连接
    return getMasterConnection();
}

std::shared_ptr<MySQL> ConnectionPool::getMasterConnection() {
    
    // 1. 获取锁：unique_lock在构造时加锁，析构时自动解锁。
    //    它比lock_guard更灵活，可以和条件变量配合。
    std::unique_lock<std::mutex> lock(masterMutex_);
    
    // 尝试创建新连接（如果池为空且未达上限） 如果连接池为空且未达到最大连接数，创建新连接
    if (masterConnections_.empty() && masterConnections_.size() < static_cast<size_t>(masterMaxSize_)) {

        // 注意：这里是在锁内创建连接！创建连接是耗时操作，会阻塞其他线程，这是一个可以优化的点。
        auto conn = createConnection(MySQL::MASTER, masterServer_);
        if (conn) {
            return conn;// 如果创建成功，直接返回这个新连接
        }
    }
    
    // 等待可用连接
    //    如果池子是空的，当前线程就会在这个条件变量上等待，并自动释放锁。
    //    当其他线程调用`masterCondition_.notify_one/notify_all()`时，它会被唤醒，并重新获取锁。
    while (masterConnections_.empty()) {
        masterCondition_.wait(lock);
    }
    // 4. 从池中取出一个连接
    auto conn = masterConnections_.front();
    masterConnections_.pop();
    
    // 5. 检查连接健康性
    //    连接可能因为网络波动、数据库重启等原因已经失效。
    if (conn && mysql_ping(conn->getConnection()) != 0) {
        // 连接已断开，创建新连接
        conn = createConnection(MySQL::MASTER, masterServer_);
    }

    // STEP 6: 返回连接 (锁会在return时通过lock的析构函数自动释放)
    return conn;
}

std::shared_ptr<MySQL> ConnectionPool::getSlaveConnection() {
    if (slaveServers_.empty()) {
        // 如果没有从库，返回主库连接
        return getMasterConnection();
    }
    
    std::unique_lock<std::mutex> lock(slaveMutex_);
    
    // 轮询选择一个从库 使用原子变量 currentSlaveIndex_ 来保证线程安全地递增。
    size_t slaveIndex = currentSlaveIndex_++ % slaveServers_.size();
    
    // 如果连接池为空且未达到最大连接数，创建新连接
    if (slaveConnections_[slaveIndex].empty() && 
        slaveConnections_[slaveIndex].size() < static_cast<size_t>(slaveMaxSize_)) {
        auto conn = createConnection(MySQL::SLAVE, slaveServers_[slaveIndex]);
        if (conn) {
            slaveConnections_[slaveIndex].push(conn);
            auto result = conn;
            slaveConnections_[slaveIndex].pop();
            return result;
        }
    }
    
    // 等待可用连接
    while (slaveConnections_[slaveIndex].empty()) {
        slaveCondition_.wait(lock);
    }
    
    auto conn = slaveConnections_[slaveIndex].front();
    slaveConnections_[slaveIndex].pop();
    
    // 检查连接是否仍然有效
    if (conn && mysql_ping(conn->getConnection()) != 0) {
        // 连接已断开，创建新连接
        conn = createConnection(MySQL::SLAVE, slaveServers_[slaveIndex]);
    }
    
    return conn;
}

std::shared_ptr<MySQL> ConnectionPool::getConnection(MySQL::DBRole role) {
    if (role == MySQL::MASTER) {
        return getMasterConnection();
    } else {
        return getSlaveConnection();
    }
}

void ConnectionPool::returnConnection(std::shared_ptr<MySQL> conn) {
    if (!conn) return; // 检查空指针
    
    //主库：直接推回 masterConnections_ 队列，然后 notify_one()。
    if (conn->getRole() == MySQL::MASTER) {
        // 简单的锁守卫，作用域内加锁解锁
        std::lock_guard<std::mutex> lock(masterMutex_);
        masterConnections_.push(conn);// 将连接推回主库队列
        masterCondition_.notify_one(); // 通知一个正在等待的线程“有连接可用了”
    } 
    //通过 getServer() 找到它的“家”（对应的队列），然后推回去，再 notify_one()。
    else {// 处理从库连接的归还
        std::lock_guard<std::mutex> lock(slaveMutex_);
        // 根据连接实际服务器归还到对应从库队列
        const std::string& server = conn->getServer();
        bool placed = false;
        for (size_t i = 0; i < slaveServers_.size(); ++i) {
            if (slaveServers_[i] == server) {//遍历查找匹配的从库配置
                slaveConnections_[i].push(conn);//找到则放入对应的队列
                placed = true;
                break;
            }
        }
        if (!placed) {
            // 如果未找到匹配（可能从库列表变化），退化策略：放回轮询当前位置队列
            size_t idx = currentSlaveIndex_ % (slaveConnections_.empty() ? 1 : slaveConnections_.size());
            if (!slaveConnections_.empty()) {
                slaveConnections_[idx].push(conn);// 4. 保底策略，防止崩溃
            }
        }
        slaveCondition_.notify_one();// 5. 通知等待线程
    }
}

size_t ConnectionPool::size() const {
    return masterSize() + slaveSize();
}


//方法被声明为 const，表示它不会修改成员变量
size_t ConnectionPool::masterSize() const {

    //这是一个简单的作用域锁，读操作也需要加锁，因为可能正有其他线程在 push 或 pop
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