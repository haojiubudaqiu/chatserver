#include "cache_manager.h"
#include <muduo/base/Logging.h>
#include "usermodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
CacheManager::CacheManager() : _redisCache(nullptr) {
    _userModel = new UserModel();
    _friendModel = new FriendModel();
    _groupModel = new GroupModel();
}

CacheManager::~CacheManager() {
    delete _userModel;
    delete _friendModel;
    delete _groupModel;
}

CacheManager* CacheManager::instance() {
    static CacheManager manager;
    return &manager;
}

bool CacheManager::init() {
    _redisCache = RedisCache::instance();
    if (!_redisCache->connect()) {
        LOG_ERROR << "Failed to connect to Redis cache";
        return false;
    }
    LOG_INFO << "Redis cache initialized (direct connection)";
    return true;
}

bool CacheManager::initWithSentinel(const std::vector<std::string>& sentinelAddrs,
                                   const std::string& masterName) {
    _redisCache = RedisCache::instance();
    if (!_redisCache->connectWithSentinel(sentinelAddrs, masterName)) {
        LOG_ERROR << "Failed to connect to Redis via Sentinel";
        return false;
    }
    LOG_INFO << "Redis cache initialized (Sentinel mode)";
    return true;
}

bool CacheManager::isUsingSentinel() const {
    if (!_redisCache) return false;
    return _redisCache->isUsingSentinel();
}

std::string CacheManager::getRedisMasterAddr() const {
    if (!_redisCache) return "";
    return _redisCache->getMasterAddr();
}

bool CacheManager::cacheUser(const User& user) {
    if (!_redisCache) return false;
    return _redisCache->setUser(user);
}

User CacheManager::getUser(int userId) {
    if (!_redisCache) return User();
    
    User user = _redisCache->getUser(userId);
    if (user.getId() != 0) {
        return user;
    }
    user = _userModel->query(userId);
    if (user.getId() != 0) {
        _redisCache->setUser(user);
    }
    
    return user;
}

bool CacheManager::invalidateUser(int userId) {
    if (!_redisCache) return false;
    return _redisCache->deleteUser(userId);
}

bool CacheManager::cacheFriends(int userId, const std::vector<User>& friends) {
    if (!_redisCache) return false;
    return _redisCache->setFriends(userId, friends);
}

std::vector<User> CacheManager::getFriends(int userId) {
    if (!_redisCache) return std::vector<User>();
    
    std::vector<User> friends = _redisCache->getFriends(userId);
    if (!friends.empty()) {
        return friends;
    }
    friends = _friendModel->query(userId);
    if (!friends.empty()) {
        _redisCache->setFriends(userId, friends);
    }
    
    return friends;
}

bool CacheManager::invalidateFriends(int userId) {
    if (!_redisCache) return false;
    return _redisCache->deleteFriends(userId);
}

bool CacheManager::cacheGroup(const Group& group) {
    if (!_redisCache) return false;
    return _redisCache->setGroup(group);
}

Group CacheManager::getGroup(int groupId) {
    if (!_redisCache) return Group();
    
    Group group = _redisCache->getGroup(groupId);
    if (group.getId() != 0) {
        return group;
    }
    group = _groupModel->queryGroup(groupId);
    if (group.getId() != 0) {
        _redisCache->setGroup(group);
    }
    
    return group;
}

bool CacheManager::invalidateGroup(int groupId) {
    if (!_redisCache) return false;
    return _redisCache->deleteGroup(groupId);
}

bool CacheManager::cacheUserStatus(int userId, const std::string& status) {
    if (!_redisCache) return false;
    return _redisCache->setUserStatus(userId, status);
}

std::string CacheManager::getUserStatus(int userId) {
    if (!_redisCache) return "";
    
    std::string status = _redisCache->getUserStatus(userId);
    if (!status.empty()) {
        return status;
    }
    User user = _userModel->query(userId);
    if (user.getId() != 0) {
        status = user.getState();
        _redisCache->setUserStatus(userId, status);
    }
    
    return status;
}

bool CacheManager::invalidateUserStatus(int userId) {
    if (!_redisCache) return false;
    return _redisCache->deleteUserStatus(userId);
}

bool CacheManager::setNx(const std::string& key, const std::string& value, int ttlSeconds) {
    if (!_redisCache) return false;
    return _redisCache->setNx(key, value, ttlSeconds);
}

