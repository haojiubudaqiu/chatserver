#include "db.h"
#include "connection_pool.h"
#include <muduo/base/Logging.h>




// 数据库配置信息
static string master_server = "127.0.0.1";// 主库地址
static string slave_servers = "127.0.0.1:3307,127.0.0.1:3308";// 从库地址列表，用逗号分隔
static string user = "root";
static string password = "Sf523416&111";
static string dbname = "chat";

// 连接池标志
bool MySQL::useConnectionPool = false;

// 初始化数据库连接
MySQL::MySQL(DBRole role)
{   
    // mysql_init(NULL) 是C API函数，分配并初始化一个MYSQL对象，为连接做准备。
    _conn = mysql_init(nullptr);
    // 将传入的角色参数保存到成员变量中
    _role = role;
}

// 释放数据库连接资源
MySQL::~MySQL()
{
    if (_conn != nullptr)
        mysql_close(_conn);
}

// 连接池初始化
bool MySQL::initConnectionPool(const std::string& server, const std::string& user,
                              const std::string& password, const std::string& dbname,
                              int port, int maxSize) {
    // 将使用连接池的标志设为 true
    useConnectionPool = true;
    // 调用 ConnectionPool 单例的 init 方法进行初始化。
    // 这里暗示存在一个 ConnectionPool 类，并且它有一个 instance() 方法返回单例对象。
    return ConnectionPool::instance()->init(server, user, password, dbname, port, maxSize);
}

// 从连接池获取连接
std::shared_ptr<MySQL> MySQL::getConnectionFromPool() {
    if (useConnectionPool) {
        // 如果启用了连接池，就从池中获取一个连接。
        // 返回的是 shared_ptr<MySQL>，当这个智能指针离开作用域被销毁时
        // 它会自动将连接归还给池子，而不是直接关闭它。
        return ConnectionPool::instance()->getConnection();
    }
    return nullptr;
}

// 连接数据库
bool MySQL::connect()
{
    // 默认连接主库
    string server_addr = master_server;
    int port = 3306;
    
    // 根据角色选择服务器
    if (_role == SLAVE) {
        // 实现一个简单的轮询 (round-robin) 算法来选择从库，以实现负载均衡。
        static int slave_index = 0; // 静态变量，记录上次选择的从库索引
        vector<string> slaves;



         // 字符串处理：将配置中的 "addr1:port1,addr2:port2" 拆分成字符串向量
        size_t pos = 0;
        string servers = slave_servers;
        string delimiter = ",";
        while ((pos = servers.find(delimiter)) != string::npos) {
            slaves.push_back(servers.substr(0, pos));
            servers.erase(0, pos + delimiter.length());
        }
        slaves.push_back(servers);
        

        // 如果配置了从库
        if (!slaves.empty()) {
            // 通过取模运算实现轮询
            string slave = slaves[slave_index % slaves.size()];
            slave_index++;// 为下一次选择做准备
            
            // 解析从服务器地址和端口
            size_t colon_pos = slave.find(":");
            if (colon_pos != string::npos) {
                server_addr = slave.substr(0, colon_pos);
                port = stoi(slave.substr(colon_pos + 1));
            }
            // 如果没有冒号，则使用默认的 server_addr 和 port 3306
        }
    }


    // 如果是主库，则直接使用 master_server 和 3306

    // 核心连接函数：mysql_real_connect
    MYSQL *p = mysql_real_connect(_conn, server_addr.c_str(), user.c_str(),
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)// 连接成功
    {
        // 设置连接字符集为 gbk，以确保正确处理中文。
        // 这是一个非常重要的步骤，否则会出现乱码。
        mysql_query(_conn, "set names gbk");

        // 保存服务器信息，供连接池归还定位使用
        _server = server_addr;
        _port = port;

        // 使用 muduo 日志库输出成功信息，包含角色和服务器地址。
        LOG_INFO << "connect mysql success! Role: " << (_role == MASTER ? "MASTER" : "SLAVE") 
                 << " Server: " << server_addr << ":" << port;
    }
    else// 连接失败
    {
        // 添加详细的错误信息输出
        LOG_ERROR << "connect mysql fail! Error: " << mysql_error(_conn);
        LOG_ERROR << "Connection details:";
        LOG_ERROR << "  Server: " << server_addr;
        LOG_ERROR << "  User: " << user;
        LOG_ERROR << "  DB Name: " << dbname;
        LOG_ERROR << "  Port: " << port;
        LOG_ERROR << "  Role: " << (_role == MASTER ? "MASTER" : "SLAVE");
    }

    return p; // 如果成功，p == _conn != nullptr；如果失败，p == nullptr。
}

// 连接数据库（指定参数） 重载的 connect 方法 使用调用者传入的参数
bool MySQL::connect(const std::string& server, const std::string& user,
                    const std::string& password, const std::string& dbname,
                    int port) {
    MYSQL *p = mysql_real_connect(_conn, server.c_str(), user.c_str(),
                                  password.c_str(), dbname.c_str(), port, nullptr, 0);
    if (p != nullptr)
    {
        mysql_query(_conn, "set names gbk");
        _server = server;
        _port = port;
        LOG_INFO << "connect mysql success! Role: " << (_role == MASTER ? "MASTER" : "SLAVE") 
                 << " Server: " << server << ":" << port;
    }
    else
    {
        LOG_ERROR << "connect mysql fail! Error: " << mysql_error(_conn);
        LOG_ERROR << "Connection details:";
        LOG_ERROR << "  Server: " << server;
        LOG_ERROR << "  User: " << user;
        LOG_ERROR << "  DB Name: " << dbname;
        LOG_ERROR << "  Port: " << port;
        LOG_ERROR << "  Role: " << (_role == MASTER ? "MASTER" : "SLAVE");
    }

    return p;
}

// 更新操作
bool MySQL::update(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "更新失败!";
        return false;
    }

    return true;
}

// 查询操作
MYSQL_RES *MySQL::query(string sql)
{
    if (mysql_query(_conn, sql.c_str()))
    {
        LOG_INFO << __FILE__ << ":" << __LINE__ << ":"
                 << sql << "查询失败!";
        return nullptr;
    }
    
    return mysql_use_result(_conn);
}

// 获取连接
MYSQL* MySQL::getConnection()
{
    return _conn;
}

// 获取连接角色
MySQL::DBRole MySQL::getRole() const
{
    return _role;
}

// 连接健康检查
bool MySQL::ping()
{
    // mysql_ping 检查连接是否还活着。
    // 如果连接断开，它会尝试自动重新连接。
    // 返回0表示连接是活跃的或者重连成功。
    return mysql_ping(_conn) == 0;
}