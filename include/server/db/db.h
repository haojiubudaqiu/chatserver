#ifndef DB_H
#define DB_H
/**
 * 这个 MySQL 类是一个对 MySQL C API 的 C++ 封装。它的主要目的是：

简化操作：将原生的、需要多个步骤的 C API 调用封装成简单的类方法（如 connect, update, query）。

资源管理：利用类的构造和析构函数自动管理数据库连接资源，避免内存泄漏。

实现主从分离：通过 DBRole 枚举，区分连接是到主库（Master）还是从库（Slave），这是读写分离架构的基础。

引入连接池概念：通过静态方法和 ConnectionPool 类（虽未在此文件中实现，但被引用）来管理多个数据库连接，以提高性能。
 * 
 */


#include <mysql/mysql.h>// MySQL C API 的头文件，提供了所有操作MySQL所需的函数和数据类型（如 MYSQL, MYSQL_RES）。
#include <string>
#include <memory>
using namespace std;

// 数据库操作类
class MySQL
{
public:
    // 1. 枚举：定义数据库角色
    enum DBRole {
        MASTER,// 主库，通常用于写操作（INSERT, UPDATE, DELETE）
        SLAVE // 从库，通常用于读操作（SELECT）
    };
    
    // 初始化数据库连接 初始化一个MySQL连接对象，并指定其角色（默认是MASTER）
    MySQL(DBRole role = MASTER);
    // 释放数据库连接资源 自动释放数据库连接资源，防止连接泄漏
    ~MySQL();

    
    // 连接数据库
    // 使用类内部预设的配置进行连接
    bool connect();
    // 连接数据库（指定参数）
    // 使用调用者提供的具体参数进行连接
    bool connect(const std::string& server, const std::string& user,
                 const std::string& password, const std::string& dbname,
                 int port = 3306);// 端口有默认值3306
    // 更新操作 执行更新语句（INSERT, UPDATE, DELETE），返回是否成功
    bool update(string sql);
    // 查询操作  执行查询语句（SELECT），返回结果集（MYSQL_RES*），需要调用者后续提取数据并释放结果集
    MYSQL_RES *query(string sql);
    // 获取连接 获取底层的 MYSQL 连接对象，用于需要直接使用C API的特殊情况
    MYSQL* getConnection();
    // 获取连接角色
    DBRole getRole() const;
    // 获取当前连接的服务器地址（不含端口）
    const std::string& getServer() const { return _server; }
    // 获取当前连接端口
    int getPort() const { return _port; }
    // 连接健康检查 检查当前连接是否仍然有效（健康检查），如果连接断开，尝试重连
    bool ping();
    
    //静态方法 - 连接池管理
    // 连接池初始化
    static bool initConnectionPool(const std::string& server, const std::string& user,
                                  const std::string& password, const std::string& dbname,
                                  int port = 3306, int maxSize = 10);
    
    // 从连接池中获取一个可用的数据库连接（包装在智能指针中，无需手动管理生命周期）
    static std::shared_ptr<MySQL> getConnectionFromPool();
    
    // 设置数据库配置
    static void setDBConfig(const std::string& master, int masterPort,
                           const std::string& slaves,
                           const std::string& user, const std::string& password, 
                           const std::string& dbname);
    
private:
    // 成员变量
    MYSQL *_conn;// 核心成员：指向一个MySQL连接结构的指针，所有操作都通过它进行。
    DBRole _role;// 记录这个连接对象的角色

    // 静态成员变量
    static bool useConnectionPool;// 一个标志位，指示整个程序是否使用连接池模式

    // 记录此连接所连接的服务器及端口，便于连接池归还时定位到对应的从库队列
    std::string _server;
    int _port {3306};
};

#endif