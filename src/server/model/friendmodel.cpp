#include "connection_guard.h"
#include "friendmodel.hpp"
#include "database_router.h"
#include "cache_manager.h"

FriendModel::FriendModel() {}

bool FriendModel::insert(int userid, int friendid)
{
    char sql[1024] = {0};
    sprintf(sql, "insert ignore into friend values(%d, %d)", userid, friendid);

    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn) {
        return false;
    }
    
    bool ok = conn->update(sql);
    if (ok) {
        CacheManager::instance()->invalidateFriends(userid);
        CacheManager::instance()->invalidateFriends(friendid);
    }
    return ok;
}

vector<User> FriendModel::query(int userid)
{
    vector<User> vec;
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid=%d", userid);

    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery());
    if (!conn) {
        return vec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            if (row[0] == nullptr) continue;
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1] ? row[1] : "");
            user.setState(row[2] ? row[2] : "offline");
            vec.push_back(user);
        }
        mysql_free_result(res);
        
        CacheManager::instance()->cacheFriends(userid, vec);
    }
    
    return vec;
}