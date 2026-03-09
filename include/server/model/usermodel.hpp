#ifndef USERMODEL_H
#define USERMODEL_H

#include "user.hpp"

// 前向声明
class CacheManager;

// User表的数据操作类
class UserModel {
public:
    UserModel();
    
    // User表的增加方法
    bool insert(User &user);

    // 根据用户号码查询用户信息
    // forceMaster: 强制读主库（用于注册后立即登录等需要强一致性的场景）
    User query(int id);
    User query(int id, bool forceMaster);

    // 更新用户的状态信息
    bool updateState(User user);

    // 重置用户的状态信息
    void resetState();

private:
    CacheManager* _cacheManager;
};

#endif