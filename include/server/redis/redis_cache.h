#ifndef REDIS_CACHE_H
#define REDIS_CACHE_H

#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "user.hpp"
#include "group.hpp"

// Redis缓存类
class RedisCache {
public:
    static RedisCache* instance();
    
    // 连接Redis服务器
    bool connect();
    
    // 设置用户信息缓存
    bool setUser(const User& user);
    
    // 获取用户信息缓存
    User getUser(int userId);
    
    // 删除用户信息缓存
    bool deleteUser(int userId);
    
    // 设置好友列表缓存
    bool setFriends(int userId, const std::vector<User>& friends);
    
    // 获取好友列表缓存
    std::vector<User> getFriends(int userId);
    
    // 删除好友列表缓存
    bool deleteFriends(int userId);
    
    // 设置群组信息缓存
    bool setGroup(const Group& group);
    
    // 获取群组信息缓存
    Group getGroup(int groupId);
    
    // 删除群组信息缓存
    bool deleteGroup(int groupId);
    
    // 设置用户在线状态缓存
    bool setUserStatus(int userId, const std::string& status);
    
    // 获取用户在线状态缓存
    std::string getUserStatus(int userId);
    
    // 删除用户在线状态缓存
    bool deleteUserStatus(int userId);
    
    // 设置离线消息计数缓存
    bool setOfflineMsgCount(int userId, int count);
    
    // 获取离线消息计数缓存
    int getOfflineMsgCount(int userId);
    
    // 删除离线消息计数缓存
    bool deleteOfflineMsgCount(int userId);

private:
    RedisCache();
    ~RedisCache();
    
    // Redis连接上下文
    redisContext* _context;
    
    // 连接锁
    std::mutex _mutex;
};

#endif