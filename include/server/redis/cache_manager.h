#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include "redis_cache.h"
#include "usermodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
#include "offlinemessagemodel.hpp"
#include <memory>

// 缓存管理器类
class CacheManager {
public:
    static CacheManager* instance();
    
    // 初始化缓存管理器
    bool init();
    
    // 用户相关缓存操作
    bool cacheUser(const User& user);
    User getUser(int userId);
    bool invalidateUser(int userId);
    
    // 好友列表缓存操作
    bool cacheFriends(int userId, const std::vector<User>& friends);
    std::vector<User> getFriends(int userId);
    bool invalidateFriends(int userId);
    
    // 群组信息缓存操作
    bool cacheGroup(const Group& group);
    Group getGroup(int groupId);
    bool invalidateGroup(int groupId);
    
    // 用户状态缓存操作
    bool cacheUserStatus(int userId, const std::string& status);
    std::string getUserStatus(int userId);
    bool invalidateUserStatus(int userId);
    
    // 离线消息计数缓存操作
    bool cacheOfflineMsgCount(int userId, int count);
    int getOfflineMsgCount(int userId);
    bool invalidateOfflineMsgCount(int userId);
    
    // 组合操作：缓存用户及其相关信息
    bool cacheUserWithRelations(const User& user, int userId);
    
private:
    CacheManager();
    ~CacheManager();
    
    RedisCache* _redisCache;
    UserModel* _userModel;
    FriendModel* _friendModel;
    GroupModel* _groupModel;
    OfflineMsgModel* _offlineMsgModel;
};

#endif