#ifndef CACHE_MANAGER_H
#define CACHE_MANAGER_H

#include "redis_cache.h"
#include <memory>

class UserModel;
class FriendModel;
class GroupModel;

class CacheManager {
public:
    static CacheManager* instance();

    bool init();
    bool initWithSentinel(const std::vector<std::string>& sentinelAddrs,
                         const std::string& masterName = "mymaster");
    bool isUsingSentinel() const;
    std::string getRedisMasterAddr() const;

    bool cacheUser(const User& user);
    User getUser(int userId);
    bool invalidateUser(int userId);

    bool cacheFriends(int userId, const std::vector<User>& friends);
    std::vector<User> getFriends(int userId);
    bool invalidateFriends(int userId);

    bool cacheGroup(const Group& group);
    Group getGroup(int groupId);
    bool invalidateGroup(int groupId);

    bool cacheUserStatus(int userId, const std::string& status);
    std::string getUserStatus(int userId);
    bool invalidateUserStatus(int userId);

    bool setNx(const std::string& key, const std::string& value, int ttlSeconds = 30);

private:
    CacheManager();
    ~CacheManager();

    RedisCache* _redisCache;
    UserModel* _userModel;
    FriendModel* _friendModel;
    GroupModel* _groupModel;
};

#endif