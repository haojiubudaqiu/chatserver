#include "redis_cache.h"
#include <cstdarg>
#include <cstdlib>
#include <muduo/base/Logging.h>

RedisCache::RedisCache() : _context(nullptr) {}

RedisCache::~RedisCache() {
    if (_context != nullptr) {
        redisFree(_context);
    }
}

RedisCache* RedisCache::instance() {
    static RedisCache cache;
    return &cache;
}

bool RedisCache::connect() {
    const char* redisHost = getenv("REDIS_HOST") ? getenv("REDIS_HOST") : "127.0.0.1";
    _context = redisConnect(redisHost, 6379);
    if (nullptr == _context || _context->err) {
        if (_context) {
            LOG_ERROR << "Redis connection error: " << _context->errstr;
            redisFree(_context);
        } else {
            LOG_ERROR << "Redis connection error: can't allocate redis context";
        }
        _context = nullptr;
        return false;
    }
    
    LOG_INFO << "Connect redis-server success!";
    return true;
}

bool RedisCache::connectWithSentinel(const std::vector<std::string>& sentinelAddrs,
                                     const std::string& masterName) {
    sentinel_ = std::make_unique<RedisSentinel>(sentinelAddrs, masterName);
    
    if (!sentinel_->connect()) {
        LOG_ERROR << "Failed to connect to sentinel cluster";
        return false;
    }
    
    auto ctx = sentinel_->getMasterConnection();
    if (!ctx) {
        LOG_ERROR << "Failed to get master connection from sentinel";
        return false;
    }
    
    _context = nullptr;
    
    sentinel_->setFailoverHandler([this](const std::string& newHost, int newPort) {
        LOG_WARN << "Redis failover detected! New master: " << newHost << ":" << newPort;
    });
    
    sentinel_->startListen();
    
    LOG_INFO << "Connect to Redis via Sentinel success! Master: " 
             << sentinel_->getMasterHost() << ":" << sentinel_->getMasterPort();
    return true;
}

redisContext* RedisCache::getContext() {
    if (sentinel_) {
        auto ctx = sentinel_->getMasterConnection();
        if (ctx) {
            return ctx.get();
        }
    }
    return _context;
}

redisReply* RedisCache::executeCommand(const char* format, ...) {
    redisContext* ctx = getContext();
    if (!ctx) return nullptr;
    
    va_list args;
    va_start(args, format);
    redisReply* reply = (redisReply*)redisvCommand(ctx, format, args);
    va_end(args);
    
    return reply;
}

bool RedisCache::setUser(const User& user) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "user:" + std::to_string(user.getId());
    
    redisReply* reply = (redisReply*)redisCommand(ctx, 
        "HMSET %s id %d name %s password %s state %s", 
        key.c_str(), 
        user.getId(), 
        user.getName().c_str(), 
        user.getPwd().c_str(), 
        user.getState().c_str());
    
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set user cache for user id: " << user.getId();
        return false;
    }
    
    freeReplyObject(reply);
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 1800", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

User RedisCache::getUser(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return User();
    
    std::string key = "user:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return User();
    }
    
    User user;
    if (reply->elements >= 8) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str);
            std::string value(reply->element[i+1]->str);
            
            if (field == "id") {
                user.setId(std::stoi(value));
            } else if (field == "name") {
                user.setName(value);
            } else if (field == "password") {
                user.setPwd(value);
            } else if (field == "state") {
                user.setState(value);
            }
        }
    }
    
    freeReplyObject(reply);
    return user;
}


bool RedisCache::deleteUser(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "user:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete user cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

//写入好友列表
bool RedisCache::setFriends(int userId, const std::vector<User>& friends) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "friends:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    for (const auto& friendUser : friends) {
        std::string friendData = std::to_string(friendUser.getId()) + ":" + 
                                friendUser.getName() + ":" + 
                                friendUser.getState();
        
        reply = (redisReply*)redisCommand(ctx, "RPUSH %s %s", key.c_str(), friendData.c_str());
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 900", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

std::vector<User> RedisCache::getFriends(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return std::vector<User>();
    
    std::string key = "friends:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "LRANGE %s 0 -1", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return std::vector<User>();
    }
    
    std::vector<User> friends;
    for (size_t i = 0; i < reply->elements; i++) {
        std::string friendData(reply->element[i]->str);
        size_t firstColon = friendData.find(':');
        size_t secondColon = friendData.find(':', firstColon + 1);
        
        if (firstColon != std::string::npos && secondColon != std::string::npos) {
            int id = std::stoi(friendData.substr(0, firstColon));
            std::string name = friendData.substr(firstColon + 1, secondColon - firstColon - 1);
            std::string state = friendData.substr(secondColon + 1);
            
            User user;
            user.setId(id);
            user.setName(name);
            user.setState(state);
            friends.push_back(user);
        }
    }
    
    freeReplyObject(reply);
    return friends;
}

bool RedisCache::deleteFriends(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "friends:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete friends cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisCache::setGroup(const Group& group) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "group:" + std::to_string(group.getId());
    
    redisReply* reply = (redisReply*)redisCommand(ctx, 
        "HMSET %s id %d groupname %s groupdesc %s", 
        key.c_str(), 
        group.getId(), 
        group.getName().c_str(), 
        group.getDesc().c_str());
    
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set group cache for group id: " << group.getId();
        return false;
    }
    
    freeReplyObject(reply);
    
    std::string membersKey = "group:members:" + std::to_string(group.getId());
    
    reply = (redisReply*)redisCommand(ctx, "DEL %s", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    const std::vector<GroupUser>& users = group.getUsers();
    for (const auto& groupUser : users) {
        std::string memberData = std::to_string(groupUser.getId()) + ":" + 
                                groupUser.getName() + ":" + 
                                groupUser.getState() + ":" + 
                                groupUser.getRole();
        
        reply = (redisReply*)redisCommand(ctx, "RPUSH %s %s", membersKey.c_str(), memberData.c_str());
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 600", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 600", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

Group RedisCache::getGroup(int groupId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return Group();
    
    std::string key = "group:" + std::to_string(groupId);
    std::string membersKey = "group:members:" + std::to_string(groupId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "HGETALL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return Group();
    }
    
    Group group;
    if (reply->elements >= 6) {
        for (size_t i = 0; i < reply->elements; i += 2) {
            std::string field(reply->element[i]->str);
            std::string value(reply->element[i+1]->str);
            
            if (field == "id") {
                group.setId(std::stoi(value));
            } else if (field == "groupname") {
                group.setName(value);
            } else if (field == "groupdesc") {
                group.setDesc(value);
            }
        }
    }
    
    freeReplyObject(reply);
    
    reply = (redisReply*)redisCommand(ctx, "LRANGE %s 0 -1", membersKey.c_str());
    if (reply != nullptr && reply->type == REDIS_REPLY_ARRAY) {
        std::vector<GroupUser> members;
        for (size_t i = 0; i < reply->elements; i++) {
            std::string memberData(reply->element[i]->str);
            size_t firstColon = memberData.find(':');
            size_t secondColon = memberData.find(':', firstColon + 1);
            size_t thirdColon = memberData.find(':', secondColon + 1);
            
            if (firstColon != std::string::npos && secondColon != std::string::npos && thirdColon != std::string::npos) {
                int id = std::stoi(memberData.substr(0, firstColon));
                std::string name = memberData.substr(firstColon + 1, secondColon - firstColon - 1);
                std::string state = memberData.substr(secondColon + 1, thirdColon - secondColon - 1);
                std::string role = memberData.substr(thirdColon + 1);
                
                GroupUser user;
                user.setId(id);
                user.setName(name);
                user.setState(state);
                user.setRole(role);
                members.push_back(user);
            }
        }
        group.setUsers(members);
    }
    
    if (reply) freeReplyObject(reply);
    return group;
}

bool RedisCache::deleteGroup(int groupId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "group:" + std::to_string(groupId);
    std::string membersKey = "group:members:" + std::to_string(groupId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    reply = (redisReply*)redisCommand(ctx, "DEL %s", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

bool RedisCache::setUserStatus(int userId, const std::string& status) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %s", key.c_str(), status.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set user status cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 300", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

std::string RedisCache::getUserStatus(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) {
        LOG_ERROR << "getUserStatus: getContext returned null for user " << userId;
        return "";
    }
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return "";
    }
    
    std::string status(reply->str);
    freeReplyObject(reply);
    return status;
}

bool RedisCache::deleteUserStatus(int userId) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete user status cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisCache::setNx(const std::string& key, const std::string& value, int ttlSeconds) {
    std::lock_guard<std::mutex> lock(_mutex);
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;

    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %s NX EX %d",
        key.c_str(), value.c_str(), ttlSeconds);
    if (reply == nullptr) {
        return false;
    }

    bool result = (reply->type == REDIS_REPLY_STATUS &&
                   strcmp(reply->str, "OK") == 0);
    freeReplyObject(reply);
    return result;
}

std::string RedisCache::getMasterAddr() const {
    if (sentinel_) {
        return sentinel_->getMasterHost() + ":" + std::to_string(sentinel_->getMasterPort());
    }
    return "127.0.0.1:6379";
}

