#include "usermodel.hpp"
#include "db.h"
#include "cache_manager.h"
#include <iostream>
using namespace std;

UserModel::UserModel() {
    _cacheManager = CacheManager::instance();
}

// User表的增加方法
bool UserModel::insert(User &user)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            user.getName().c_str(), user.getPwd().c_str(), user.getState().c_str());

    // 使用连接池获取数据库连接
    auto conn = MySQL::getConnectionFromPool();
    MySQL mysql;
    if (conn) {
        // 使用连接池中的连接
        if (conn->update(sql))
        {
            // 获取插入成功的用户数据生成的主键id
            user.setId(mysql_insert_id(conn->getConnection()));
            
            // 将新用户信息缓存到Redis
            _cacheManager->cacheUser(user);
            
            return true;
        }
    } else {
        // 回退到原来的连接方式
        if (mysql.connect())
        {
            if (mysql.update(sql))
            {
                // 获取插入成功的用户数据生成的主键id
                user.setId(mysql_insert_id(mysql.getConnection()));
                
                // 将新用户信息缓存到Redis
                _cacheManager->cacheUser(user);
                
                return true;
            }
        }
    }

    return false;
}

// 根据用户号码查询用户信息
User UserModel::query(int id)
{
    // 先从缓存中查询用户信息
    User user = _cacheManager->getUser(id);
    if (user.getId() != 0) {
        return user;
    }
    
    // 缓存未命中，从数据库查询
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                User user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setPwd(row[2]);
                user.setState(row[3]);
                mysql_free_result(res);
                
                // 将查询结果缓存到Redis
                _cacheManager->cacheUser(user);
                
                return user;
            }
        }
    }

    return User();
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            // 更新缓存中的用户状态
            _cacheManager->cacheUserStatus(user.getId(), user.getState());
            _cacheManager->invalidateUser(user.getId());
            
            return true;
        }
    }
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    // 1.组装sql语句
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
        
        // 清除所有用户相关的缓存
        // 注意：在实际应用中，可能需要更精细的缓存清理策略
    }
}