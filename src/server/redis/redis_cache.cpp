#include "redis_cache.h"
#include <iostream>
#include <sstream>
#include <cstdarg>
#include <cstdlib>
#include <muduo/base/Logging.h>

/*
redis_cache.cpp 实现了 RedisCache 类，
用于在 C++ 服务器端和 Redis 数据库进行高效的数据缓存交互。
它封装了对 Redis 的连接、用户/好友/群组/状态/离线消息等常用功能的读写操作，
是业务逻辑和 Redis 之间的桥梁。
这个实现让 Redis 成为你的聊天服务器高效的内存数据库，所有和用户、好友、群组、状态、离线消息有关的数据都可以用极低延迟读写，
大幅提升了系统响应速度和并发性能。代码结构清晰，扩展性和维护性很高，
是实际项目中非常典型、实用的 Redis 缓存操作封装。
支持两种连接模式：
1. 直连模式：connect() 直接连接 Redis
2. 哨兵模式：connectWithSentinel() 通过哨兵集群连接，支持高可用
*/



RedisCache::RedisCache() : _context(nullptr) {}

RedisCache::~RedisCache() {
    if (_context != nullptr) {
        redisFree(_context);
    }
}

//使用 C++ 局部静态变量创建全局唯一的 RedisCache 实例，方便整个项目统一调用缓存功能
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
    
    _context = ctx.get();
    
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
    redisReply* reply = (redisReply*)redisCommand(ctx, format, args);
    va_end(args);
    
    return reply;
}

bool RedisCache::setUser(const User& user) {
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
    redisContext* ctx = getContext();
    if (ctx == nullptr) return "";
    
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

bool RedisCache::setOfflineMsgCount(int userId, int count) {
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "SET %s %d", key.c_str(), count);
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set offline message count cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    
    reply = (redisReply*)redisCommand(ctx, "EXPIRE %s 120", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

int RedisCache::getOfflineMsgCount(int userId) {
    redisContext* ctx = getContext();
    if (ctx == nullptr) return 0;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "GET %s", key.c_str());
    if (reply == nullptr) {
        return 0;
    }
    
    if (reply->type == REDIS_REPLY_NIL) {
        freeReplyObject(reply);
        return 0;
    }
    
    int count = 0;
    if (reply->type == REDIS_REPLY_STRING) {
        count = std::stoi(reply->str);
    }
    
    freeReplyObject(reply);
    return count;
}

std::string RedisCache::getMasterAddr() const {
    if (sentinel_) {
        return sentinel_->getMasterHost() + ":" + std::to_string(sentinel_->getMasterPort());
    }
    return "127.0.0.1:6379";
}

bool RedisCache::deleteOfflineMsgCount(int userId) {
    redisContext* ctx = getContext();
    if (ctx == nullptr) return false;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(ctx, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete offline message count cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}