#include "connection_guard.h"
#include "usermodel.hpp"
#include "database_router.h"
#include "cache_manager.h"
using namespace std;

UserModel::UserModel() {}

// User表的增加方法
bool UserModel::insert(User &user)
{
    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
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
        CacheManager::instance()->cacheUser(user);
        
        // 归还连接
        return true;
    }
    
    return false;
}

// 根据用户号码查询用户信息
// forceMaster: 强制读主库（用于注册后立即登录等场景）
User UserModel::query(int id, bool forceMaster)
{
    char sql[1024] = {0};
    sprintf(sql, "select * from user where id = %d", id);

    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery(forceMaster));
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
            return user;
        }
        mysql_free_result(res);
    }
    
    return User();
}

// 根据用户名称查询用户信息
User UserModel::queryByName(const string& name)
{
    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery(true));
    if (!conn) {
        return User();
    }
    
    char name_escaped[256];
    mysql_real_escape_string(conn->getConnection(), name_escaped, name.c_str(), name.length());
    
    char sql[1024] = {0};
    sprintf(sql, "select * from user where name = '%s'", name_escaped);

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
            
            CacheManager::instance()->cacheUser(user);
            
            return user;
        }
        mysql_free_result(res);
    }
    
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
    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn) {
        return false;
    }
    
    if (conn->update(sql))
    {
        // 更新Redis缓存
        CacheManager::instance()->cacheUserStatus(user.getId(), user.getState());
        
        return true;
    }
    
    return false;
}

// 重置用户的状态信息
void UserModel::resetState()
{
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (conn) {
        conn->update(sql);
    }
}
