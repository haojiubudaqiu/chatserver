#include "friendmodel.hpp"
#include "db.h"

FriendModel::FriendModel() {
    _cacheManager = CacheManager::instance();
}

// 添加好友关系
void FriendModel::insert(int userid, int friendid)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into friend values(%d, %d)", userid, friendid);

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
        
        // 清除好友列表缓存
        _cacheManager->invalidateFriends(userid);
        _cacheManager->invalidateFriends(friendid);
    }
}

// 返回用户好友列表
vector<User> FriendModel::query(int userid)
{
    // 先从缓存中查询好友列表
    vector<User> vec = _cacheManager->getFriends(userid);
    if (!vec.empty()) {
        return vec;
    }
    
    // 缓存未命中，从数据库查询
    // 1.组装sql语句
    char sql[1024] = {0};

    sprintf(sql, "select a.id,a.name,a.state from user a inner join friend b on b.friendid = a.id where b.userid=%d", userid);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            // 把userid用户的所有离线消息放入vec中返回
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
            
            // 将查询结果缓存到Redis
            _cacheManager->cacheFriends(userid, vec);
            
            return vec;
        }
    }
    return vec;
}