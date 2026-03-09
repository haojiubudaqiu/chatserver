#ifndef REDIS_CACHE_H
#define REDIS_CACHE_H


/*
这个头文件定义了一个Redis缓存管理类，
用于把用户、好友、群组等信息存储到 Redis 数据库里，
起到缓存加速和数据同步的作用。
它属于服务器端的缓存层，实现了和 Redis 的交互。
这个类是Redis缓存管理器，用来把用户、好友、群组、在线状态、离线消息等信息存到 Redis，不直接存到数据库
所有缓存相关操作都通过这个类实现，方便维护和统一管理。
*/


#include <hiredis/hiredis.h> //C/C++ 操作 Redis 的库。
#include <string>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>
#include "user.hpp" //定义了用户和群组的数据结构。
#include "group.hpp"
#include "redis_sentinel.h"

// Redis缓存类
class RedisCache {
public:
    //提供一个全局唯一实例
    static RedisCache* instance();
    
    // 连接Redis服务器（直连模式）
    bool connect();
    
    // 连接Redis服务器（哨兵模式）
    // sentinelAddrs: 哨兵地址列表
    // masterName: 主库名称（默认 mymaster）
    bool connectWithSentinel(const std::vector<std::string>& sentinelAddrs,
                           const std::string& masterName = "mymaster");
    
    // 检查是否使用哨兵模式
    bool isUsingSentinel() const { return sentinel_ != nullptr; }
    
    // 设置用户信息缓存 把用户信息写入 Redis（比如注册、修改信息时）。
    bool setUser(const User& user);
    
    // 获取用户信息缓存 从 Redis 读取指定用户的信息（比如登录、展示资料等）。
    User getUser(int userId); 
    
    // 删除用户信息缓存 从 Redis 删除指定用户的信息缓存（比如用户注销）。
    bool deleteUser(int userId);
    
    // 设置好友列表缓存 缓存某个用户的好友列表。
    bool setFriends(int userId, const std::vector<User>& friends);
     
    // 获取好友列表缓存从 Redis 读取某个用户的好友列表。
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
    
    // 设置离线消息计数缓存 设置用户的未读消息数量。
    bool setOfflineMsgCount(int userId, int count);
    
    // 获取离线消息计数缓存 读取用户的未读消息数量（比如登录时提示有多少条未读）
    int getOfflineMsgCount(int userId);
    
    // 删除离线消息计数缓存 清理未读消息计数缓存。
    bool deleteOfflineMsgCount(int userId);

    // 获取当前主库地址
    std::string getMasterAddr() const;

private:
    RedisCache();
    ~RedisCache();
    
    // 获取Redis连接上下文
    redisContext* getContext();
    
    // 执行Redis命令的辅助方法
    redisReply* executeCommand(const char* format, ...);
    
    // Redis连接上下文
    redisContext* _context;
    
    // 连接锁 互斥锁，确保多线程安全访问 Redis 连接。
    std::mutex _mutex;
    
    // 哨兵客户端（用于高可用）
    std::unique_ptr<RedisSentinel> sentinel_;
};

#endif