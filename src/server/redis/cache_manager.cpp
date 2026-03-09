#include "cache_manager.h"
#include <muduo/base/Logging.h>
#include "usermodel.hpp"
#include "friendmodel.hpp"
#include "groupmodel.hpp"
#include "offlinemessagemodel.hpp"

/*
缓存管理层实现，
它在数据库和Redis缓存之间提供了一个智能的中间层。
CacheManager 类是一个高级缓存抽象层，它：

封装了底层的数据模型操作（UserModel, FriendModel等）

集成了Redis缓存功能

实现了经典的"缓存-数据库"读写策略

提供了统一的API给业务层使用
*/


//初始化所有数据模型对象，但Redis缓存对象初始化为nullptr（需要在init()中初始化）
CacheManager::CacheManager() : _redisCache(nullptr) {
    _userModel = new UserModel();
    _friendModel = new FriendModel();
    _groupModel = new GroupModel();
    _offlineMsgModel = new OfflineMsgModel();
}

CacheManager::~CacheManager() {
    if (_userModel) delete _userModel;
    if (_friendModel) delete _friendModel;
    if (_groupModel) delete _groupModel;
    if (_offlineMsgModel) delete _offlineMsgModel;
}

CacheManager* CacheManager::instance() {
    static CacheManager manager;//使用局部静态变量实现单例模式，线程安全
    return &manager;
}

//一般在服务器启动时调用一次，连接 Redis 缓存，如果失败则返回false。
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

//把User对象存入 Redis 缓存。
bool CacheManager::cacheUser(const User& user) {
    if (!_redisCache) return false;
    return _redisCache->setUser(user);
}

User CacheManager::getUser(int userId) {
    if (!_redisCache) return User();
    
    // 先从缓存获取
    User user = _redisCache->getUser(userId);
    if (user.getId() != 0) {
        return user;// 缓存命中，直接返回
    }
    
    // 缓存未命中，从数据库获取
    user = _userModel->query(userId);
    if (user.getId() != 0) {
        // 将数据存入缓存
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
    
    // 先从缓存获取
    std::vector<User> friends = _redisCache->getFriends(userId);
    if (!friends.empty()) {
        return friends;
    }
    
    // 缓存未命中，从数据库获取
    friends = _friendModel->query(userId);
    if (!friends.empty()) {
        // 将数据存入缓存
        _redisCache->setFriends(userId, friends);
    }
    
    return friends;
}

//用于主动使缓存失效
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
    
    // 先从缓存获取
    Group group = _redisCache->getGroup(groupId);
    if (group.getId() != 0) {
        return group;
    }
    
    // 缓存未命中，从数据库获取
    group = _groupModel->queryGroup(groupId);
    if (group.getId() != 0) {
        // 将数据存入缓存
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
    
    // 先从缓存获取
    std::string status = _redisCache->getUserStatus(userId);
    if (!status.empty()) {
        return status;
    }
    
    // 缓存未命中，从数据库获取用户状态
    User user = _userModel->query(userId);
    if (user.getId() != 0) {
        status = user.getState();
        // 将数据存入缓存
        _redisCache->setUserStatus(userId, status);
    }
    
    return status;
}

bool CacheManager::invalidateUserStatus(int userId) {
    if (!_redisCache) return false;
    return _redisCache->deleteUserStatus(userId);
}

bool CacheManager::cacheOfflineMsgCount(int userId, int count) {
    if (!_redisCache) return false;
    return _redisCache->setOfflineMsgCount(userId, count);
}

int CacheManager::getOfflineMsgCount(int userId) {
    if (!_redisCache) return 0;
    
    // 先从缓存获取
    int count = _redisCache->getOfflineMsgCount(userId);
    if (count >= 0) {
        return count;
    }
    
    // 缓存未命中，从数据库获取离线消息计数
    // 这里需要实现离线消息计数的逻辑
    // 暂时返回0
    return 0;
}

bool CacheManager::invalidateOfflineMsgCount(int userId) {
    if (!_redisCache) return false;
    return _redisCache->deleteOfflineMsgCount(userId);
}

bool CacheManager::cacheUserWithRelations(const User& user, int userId) {
    if (!_redisCache) return false;
    
    bool result = true;
    
    // 缓存用户信息
    result &= _redisCache->setUser(user);
    
    // 缓存好友列表
    std::vector<User> friends = _friendModel->query(userId);
    if (!friends.empty()) {
        result &= _redisCache->setFriends(userId, friends);
    }
    
    // 缓存用户状态
    result &= _redisCache->setUserStatus(userId, user.getState());
    
    return result;
}