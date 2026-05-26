#include "connection_guard.h"
#include "usermodel.hpp"
#include "database_router.h"
#include "cache_manager.h"
using namespace std;

UserModel::UserModel() {}

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
        user.setId(mysql_insert_id(conn->getConnection()));
        CacheManager::instance()->cacheUser(user);
        return true;
    }
    
    return false;
}

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

User UserModel::query(int id)
{
    return query(id, false);
}

bool UserModel::updateState(User user)
{
    char sql[1024] = {0};
    sprintf(sql, "update user set state = '%s' where id = %d", user.getState().c_str(), user.getId());

    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn) {
        return false;
    }

    if (conn->update(sql))
    {
        CacheManager::instance()->cacheUserStatus(user.getId(), user.getState());
        return true;
    }
    
    return false;
}

void UserModel::resetState()
{
    char sql[1024] = "update user set state = 'offline' where state = 'online'";

    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (conn) {
        conn->update(sql);
    }
}
