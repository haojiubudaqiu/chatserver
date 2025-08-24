#ifndef DB_H
#define DB_H

#include <mysql/mysql.h>
#include <string>
using namespace std;

// 数据库操作类
class MySQL
{
public:
    // 初始化数据库连接
    MySQL();
    // 释放数据库连接资源
    ~MySQL();
    // 连接数据库
    bool connect();
    // 更新操作
    bool update(string sql);
    // 查询操作
    MYSQL_RES *query(string sql);
    // 获取连接
    MYSQL* getConnection();
    
    // 连接池初始化
    static bool initConnectionPool(const std::string& server, const std::string& user,
                                  const std::string& password, const std::string& dbname,
                                  int port = 3306, int maxSize = 10);
    
    // 从连接池获取连接
    static std::shared_ptr<MySQL> getConnectionFromPool();
    
private:
    MYSQL *_conn;
    static bool useConnectionPool;
};

#endif