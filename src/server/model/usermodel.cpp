#include "usermodel.hpp"
#include "database_router.h"
#include "cache_manager.h"
#include <iostream>
using namespace std;

UserModel::UserModel() {
    _cacheManager = CacheManager::instance();
}

// User表的增加方法
bool UserModel::insert(User &user)
{
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn) return false;

    char name_escaped[256];
    char pwd_escaped[256];
    mysql_real_escape_string(conn->getConnection(), name_escaped, user.getName().c_str(), user.getName().length());
    mysql_real_escape_string(conn->getConnection(), pwd_escaped, user.getPwd().c_str(), user.getPwd().length());

    char sql[1024] = {0};
    sprintf(sql, "insert into user(name, password, state) values('%s', '%s', '%s')",
            name_escaped, pwd_escaped, user.getState().c_str());

    if (conn->update(sql))
    {
        // 获取插入成功的用户数据生成的主键id
        user.setId(mysql_insert_id(conn->getConnection()));
        
        // 将新用户信息缓存到Redis（高频访问数据）
        _cacheManager->cacheUser(user);
        
        // 归还连接
        DatabaseRouter::instance()->returnConnection(conn);
        return true;
    }
    
    DatabaseRouter::instance()->returnConnection(conn);
    return false;
}

// 根据用户号码查询用户信息
// forceMaster: 强制读主库（用于注册后立即登录等场景）
User UserModel::query(int id, bool forceMaster)
{
    // 1. 先从Redis缓存查询（高频访问数据）
    User cachedUser = _cacheManager->getUser(id);
    if (cachedUser.getId() != 0) {
        return cachedUser;
    }
    
    // 2. 缓存未命中，从数据库查询
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    // 使用DatabaseRouter获取连接（读操作，默认从库）
    auto conn = DatabaseRouter::instance()->routeQuery(forceMaster);
    if (!conn) {
        return User();
    }
    
    MYSQL_RES *res = conn->query(sql);
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
            
            _cacheManager->cacheUser(user);
            
            DatabaseRouter::instance()->returnConnection(conn);
            return user;
        }
        mysql_free_result(res);
    }
    
    if (conn) DatabaseRouter::instance()->returnConnection(conn);
    return User();
}

// 兼容旧接口，默认不强制读主库
User UserModel::query(int id)
{
    return query(id, false);
}

// 更新用户的状态信息
bool UserModel::updateState(User user)
{
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    // 写操作，使用主库
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn) {
        return false;
    }
    
    if (conn->update(sql))
    {
        // 更新Redis缓存
        _cacheManager->cacheUserStatus(user.getId(), user.getState());
        
        DatabaseRouter::instance()->returnConnection(conn);
        return true;
    }
    
    DatabaseRouter::instance()->returnConnection(conn);
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (conn) {
        conn->update(sql);
        DatabaseRouter::instance()->returnConnection(conn);
    }
    
    // 清除所有用户相关的缓存
    // 注意：在实际应用中，可能需要更精细的缓存清理策略
}
