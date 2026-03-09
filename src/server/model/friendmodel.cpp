#include "friendmodel.hpp"
#include "database_router.h"
#include "cache_manager.h"

FriendModel::FriendModel() {
    _cacheManager = CacheManager::instance();
}

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    char sql[1024] = {0};
    sprintf(sql, "insert into friend values(%d, %d)", userid, friendid);

    // 写操作，使用主库
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (conn) {
        conn->update(sql);
        DatabaseRouter::instance()->returnConnection(conn);
    }
    
    // 清除好友列表缓存（高频访问数据）
    _cacheManager->invalidateFriends(userid);
    _cacheManager->invalidateFriends(friendid);
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid)
{
    // 1. 先从Redis缓存查询（好友列表是高频访问数据）
    vector<User> vec = _cacheManager->getFriends(userid);
    if (!vec.empty()) {
        return vec;
    }
    
    // 2. 缓存未命中，从数据库查询（读操作，从库）
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid=%d", userid);

    auto conn = DatabaseRouter::instance()->routeQuery();
    if (!conn) {
        return vec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            User user;
            user.setId(atoi(row[0]));
            user.setName(row[1]);
            user.setState(row[2]);
            vec.push_back(user);
        }
        mysql_free_result(res);
        
        // 缓存到Redis
        _cacheManager->cacheFriends(userid, vec);
    }
    
    DatabaseRouter::instance()->returnConnection(conn);
    return vec;
}
