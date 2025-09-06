#include "redis_cache.h"
#include "user.hpp"
#include <iostream>
#include <vector>

int main() {
    // 测试Redis缓存功能
    RedisCache* cache = RedisCache::instance();
    
    if (!cache->connect()) {
        std::cout << "Failed to connect to Redis" << std::endl;
        return -1;
    }
    
    std::cout << "Connected to Redis successfully" << std::endl;
    
    // 测试用户缓存
    User user;
    user.setId(1001);
    user.setName("testuser");
    user.setPwd("123456");
    user.setState("online");
    
    if (cache->setUser(user)) {
        std::cout << "Set user cache successfully" << std::endl;
        
        // 获取用户缓存
        User cachedUser = cache->getUser(1001);
        if (cachedUser.getId() != 0) {
            std::cout << "Get user cache successfully:" << std::endl;
            std::cout << "  ID: " << cachedUser.getId() << std::endl;
            std::cout << "  Name: " << cachedUser.getName() << std::endl;
            std::cout << "  State: " << cachedUser.getState() << std::endl;
        } else {
            std::cout << "Failed to get user cache" << std::endl;
        }
    } else {
        std::cout << "Failed to set user cache" << std::endl;
    }
    
    // 测试好友列表缓存
    std::vector<User> friends;
    User friend1;
    friend1.setId(1002);
    friend1.setName("friend1");
    friend1.setState("online");
    friends.push_back(friend1);
    
    User friend2;
    friend2.setId(1003);
    friend2.setName("friend2");
    friend2.setState("offline");
    friends.push_back(friend2);
    
    if (cache->setFriends(1001, friends)) {
        std::cout << "Set friends cache successfully" << std::endl;
        
        // 获取好友列表缓存
        std::vector<User> cachedFriends = cache->getFriends(1001);
        if (!cachedFriends.empty()) {
            std::cout << "Get friends cache successfully:" << std::endl;
            for (const auto& friendUser : cachedFriends) {
                std::cout << "  Friend ID: " << friendUser.getId() 
                         << ", Name: " << friendUser.getName() 
                         << ", State: " << friendUser.getState() << std::endl;
            }
        } else {
            std::cout << "Failed to get friends cache" << std::endl;
        }
    } else {
        std::cout << "Failed to set friends cache" << std::endl;
    }
    
    return 0;
}