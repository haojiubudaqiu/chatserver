#include "database_router.h"
#include <muduo/base/Logging.h>

DatabaseRouter* DatabaseRouter::instance_ = nullptr;
std::mutex DatabaseRouter::mutex_;

DatabaseRouter::DatabaseRouter() {}

DatabaseRouter::~DatabaseRouter() {}

/*
双重检查锁定模式：这是实现线程安全单例的经典模式。

第一次检查（第3行）：如果实例已经存在，直接返回，避免了不必要的加锁开销。

加锁（第4行）：确保只有一个线程能进入创建阶段。

第二次检查（第5行）：防止多个线程同时通过第一次检查后，重复创建实例。

这种模式在保证线程安全的同时，最大程度地减少了性能开销。
*/


DatabaseRouter* DatabaseRouter::instance() {
    if (instance_ == nullptr) {// 第一次检查（无锁），提高性能
        std::lock_guard<std::mutex> lock(mutex_);// 加锁
        if (instance_ == nullptr) {// 第二次检查（有锁），确保线程安全
            instance_ = new DatabaseRouter();
        }
    }
    return instance_;
}

//所有更新操作都必须发送到主库，所以直接请求主库连接。
std::shared_ptr<MySQL> DatabaseRouter::routeUpdate() {
    return ConnectionPool::instance()->getMasterConnection();
}

//条件路由：根据 preferMaster 参数的值决定路由策略
std::shared_ptr<MySQL> DatabaseRouter::routeQuery(bool preferMaster) {
    if (preferMaster) {
        return ConnectionPool::instance()->getMasterConnection();
    }
    return ConnectionPool::instance()->getSlaveConnection();
}


//绕过自动路由：允许应用程序完全控制连接类型，用于特殊场景。
std::shared_ptr<MySQL> DatabaseRouter::getConnection(MySQL::DBRole role) {
    return ConnectionPool::instance()->getConnection(role);
}

void DatabaseRouter::returnConnection(std::shared_ptr<MySQL> conn) {
    if (conn) {
        ConnectionPool::instance()->returnConnection(conn);
    }
}