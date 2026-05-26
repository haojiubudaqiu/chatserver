#ifndef REDIS_CACHE_H
#define REDIS_CACHE_H

#include <hiredis/hiredis.h>
#include <string>
#include <memory>
#include <mutex>
#include <vector>
#include "user.hpp"
#include "group.hpp"
#include "redis_sentinel.h"

class RedisCache {
public:
    static RedisCache* instance();

    bool connect();
    bool connectWithSentinel(const std::vector<std::string>& sentinelAddrs,
                           const std::string& masterName = "mymaster");
    bool isUsingSentinel() const { return sentinel_ != nullptr; }

    bool setUser(const User& user);
    User getUser(int userId);
    bool deleteUser(int userId);

    bool setFriends(int userId, const std::vector<User>& friends);
    std::vector<User> getFriends(int userId);
    bool deleteFriends(int userId);

    bool setGroup(const Group& group);
    Group getGroup(int groupId);
    bool deleteGroup(int groupId);

    bool setUserStatus(int userId, const std::string& status);
    std::string getUserStatus(int userId);
    bool deleteUserStatus(int userId);

    bool setNx(const std::string& key, const std::string& value, int ttlSeconds = 30);
    std::string getMasterAddr() const;

private:
    RedisCache();
    ~RedisCache();

    redisContext* getContext();
    redisReply* executeCommand(const char* format, ...);

    redisContext* _context;
    std::mutex _mutex;
    std::unique_ptr<RedisSentinel> sentinel_;
};

#endif