#include "redis_cache.h"
#include <iostream>
#include <sstream>
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
    // 连接Redis服务器
    _context = redisConnect("127.0.0.1", 6379);
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

bool RedisCache::setUser(const User& user) {
    if (_context == nullptr) return false;
    
    // 使用hash存储用户信息
    std::string key = "user:" + std::to_string(user.getId());
    
    redisReply* reply = (redisReply*)redisCommand(_context, 
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
    
    // 设置过期时间30分钟
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 1800", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

User RedisCache::getUser(int userId) {
    if (_context == nullptr) return User();
    
    std::string key = "user:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "HGETALL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return User();
    }
    
    User user;
    if (reply->elements >= 8) {
        // 解析hash字段
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
    if (_context == nullptr) return false;
    
    std::string key = "user:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete user cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisCache::setFriends(int userId, const std::vector<User>& friends) {
    if (_context == nullptr) return false;
    
    std::string key = "friends:" + std::to_string(userId);
    
    // 先删除旧的数据
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    // 添加好友列表
    for (const auto& friendUser : friends) {
        std::string friendData = std::to_string(friendUser.getId()) + ":" + 
                                friendUser.getName() + ":" + 
                                friendUser.getState();
        
        reply = (redisReply*)redisCommand(_context, "RPUSH %s %s", key.c_str(), friendData.c_str());
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
    
    // 设置过期时间15分钟
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 900", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

std::vector<User> RedisCache::getFriends(int userId) {
    if (_context == nullptr) return std::vector<User>();
    
    std::string key = "friends:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "LRANGE %s 0 -1", key.c_str());
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
    if (_context == nullptr) return false;
    
    std::string key = "friends:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete friends cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisCache::setGroup(const Group& group) {
    if (_context == nullptr) return false;
    
    std::string key = "group:" + std::to_string(group.getId());
    
    // 使用hash存储群组基本信息
    redisReply* reply = (redisReply*)redisCommand(_context, 
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
    
    // 存储群组成员列表
    std::string membersKey = "group:members:" + std::to_string(group.getId());
    
    // 先删除旧的成员数据
    reply = (redisReply*)redisCommand(_context, "DEL %s", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    // 添加成员列表
    const std::vector<GroupUser>& users = group.getUsers();
    for (const auto& groupUser : users) {
        std::string memberData = std::to_string(groupUser.getId()) + ":" + 
                                groupUser.getName() + ":" + 
                                groupUser.getState() + ":" + 
                                groupUser.getRole();
        
        reply = (redisReply*)redisCommand(_context, "RPUSH %s %s", membersKey.c_str(), memberData.c_str());
        if (reply != nullptr) {
            freeReplyObject(reply);
        }
    }
    
    // 设置过期时间10分钟
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 600", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 600", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

Group RedisCache::getGroup(int groupId) {
    if (_context == nullptr) return Group();
    
    std::string key = "group:" + std::to_string(groupId);
    std::string membersKey = "group:members:" + std::to_string(groupId);
    
    // 获取群组基本信息
    redisReply* reply = (redisReply*)redisCommand(_context, "HGETALL %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_ARRAY) {
        if (reply) freeReplyObject(reply);
        return Group();
    }
    
    Group group;
    if (reply->elements >= 6) {
        // 解析hash字段
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
    
    // 获取群组成员列表
    reply = (redisReply*)redisCommand(_context, "LRANGE %s 0 -1", membersKey.c_str());
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
    if (_context == nullptr) return false;
    
    std::string key = "group:" + std::to_string(groupId);
    std::string membersKey = "group:members:" + std::to_string(groupId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    reply = (redisReply*)redisCommand(_context, "DEL %s", membersKey.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

bool RedisCache::setUserStatus(int userId, const std::string& status) {
    if (_context == nullptr) return false;
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "SET %s %s", key.c_str(), status.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set user status cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    
    // 设置过期时间5分钟
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 300", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

std::string RedisCache::getUserStatus(int userId) {
    if (_context == nullptr) return "";
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "GET %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return "";
    }
    
    std::string status(reply->str);
    freeReplyObject(reply);
    return status;
}

bool RedisCache::deleteUserStatus(int userId) {
    if (_context == nullptr) return false;
    
    std::string key = "user:status:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete user status cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}

bool RedisCache::setOfflineMsgCount(int userId, int count) {
    if (_context == nullptr) return false;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "SET %s %d", key.c_str(), count);
    if (reply == nullptr) {
        LOG_ERROR << "Failed to set offline message count cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    
    // 设置过期时间2分钟
    reply = (redisReply*)redisCommand(_context, "EXPIRE %s 120", key.c_str());
    if (reply != nullptr) {
        freeReplyObject(reply);
    }
    
    return true;
}

int RedisCache::getOfflineMsgCount(int userId) {
    if (_context == nullptr) return 0;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "GET %s", key.c_str());
    if (reply == nullptr || reply->type != REDIS_REPLY_STRING) {
        if (reply) freeReplyObject(reply);
        return 0;
    }
    
    int count = std::stoi(reply->str);
    freeReplyObject(reply);
    return count;
}

bool RedisCache::deleteOfflineMsgCount(int userId) {
    if (_context == nullptr) return false;
    
    std::string key = "offline:count:" + std::to_string(userId);
    
    redisReply* reply = (redisReply*)redisCommand(_context, "DEL %s", key.c_str());
    if (reply == nullptr) {
        LOG_ERROR << "Failed to delete offline message count cache for user id: " << userId;
        return false;
    }
    
    freeReplyObject(reply);
    return true;
}