#include "cache_manager.h"
#include <muduo/base/Logging.h>

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
    static CacheManager manager;
    return &manager;
}

bool CacheManager::init() {
    _redisCache = RedisCache::instance();
    if (!_redisCache->connect()) {
        LOG_ERROR << "Failed to connect to Redis cache";
        return false;
    }
    return true;
}

bool CacheManager::cacheUser(const User& user) {
    if (!_redisCache) return false;
    return _redisCache->setUser(user);
}

User CacheManager::getUser(int userId) {
    if (!_redisCache) return User();
    
    // 先从缓存获取
    User user = _redisCache->getUser(userId);
    if (user.getId() != 0) {
        return user;
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