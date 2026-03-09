#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H



/*
这个文件定义了一个缓存管理器类（CacheManager），用于统一管理用户、好友、群组、用户状态、离线消息等各种缓存操作。
它是业务逻辑与底层缓存系统（比如 Redis）之间的桥梁，负责将常用的数据高效地存储、读取、清理缓存，提升系统性能和响应速度。
集中管理缓存逻辑：所有和缓存相关的操作都统一通过 CacheManager 管理，业务代码只需调用它，减少重复代码。
解耦业务与底层缓存实现：业务层无需关心 Redis 的具体细节，只管调用 CacheManager。
//支持批量和增量刷新：比如 cacheUserWithRelations 支持一次性同步用户所有相关数据，适合数据批量初始化或定时同步。
*/


#include "redis_cache.h" //底层 Redis 缓存操作类，封装了具体的 Redis 交互。
#include "offlinemessagemodel.hpp"
#include <memory>

// 前向声明 以便在后续指针声明时可以减少不必要的依赖，提升编译效率。
class UserModel;
class FriendModel;
class GroupModel;

// 缓存管理器类
class CacheManager {
public:
    static CacheManager* instance();
    
    // 初始化缓存管理器，比如连接 Redis，初始化各数据模型等。一般在服务器启动时调用一次。
    bool init();
    
    // 使用哨兵模式初始化缓存管理器（高可用）
    // sentinelAddrs: 哨兵地址列表
    // masterName: 主库名称（默认 mymaster）
    bool initWithSentinel(const std::vector<std::string>& sentinelAddrs,
                         const std::string& masterName = "mymaster");
    
    // 检查是否使用哨兵模式
    bool isUsingSentinel() const;
    
    // 获取当前 Redis 主库地址
    std::string getRedisMasterAddr() const;
    
    // 用户相关缓存操作
    bool cacheUser(const User& user); //将用户信息存入缓存（比如用户登录、修改信息后）
    User getUser(int userId); //从缓存获取指定用户的信息（比查数据库快很多）。
    bool invalidateUser(int userId); //删除某个用户的缓存（比如用户信息变更或注销时）。
    
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
    
    //各种操作对象
    RedisCache* _redisCache;
    UserModel* _userModel;
    FriendModel* _friendModel;
    GroupModel* _groupModel;
    OfflineMsgModel* _offlineMsgModel;
};

#endif