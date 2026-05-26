#include "database_router.h"
#include <muduo/base/Logging.h>

DatabaseRouter* DatabaseRouter::instance_ = nullptr;
std::mutex DatabaseRouter::mutex_;

DatabaseRouter::DatabaseRouter() {}

DatabaseRouter::~DatabaseRouter() {}

DatabaseRouter* DatabaseRouter::instance() {
    if (instance_ == nullptr) {
        std::lock_guard<std::mutex> lock(mutex_);
        if (instance_ == nullptr) {
            instance_ = new DatabaseRouter();
        }
    }
    return instance_;
}

std::shared_ptr<MySQL> DatabaseRouter::routeUpdate() {
    return ConnectionPool::instance()->getMasterConnection();
}

std::shared_ptr<MySQL> DatabaseRouter::routeQuery(bool preferMaster) {
    if (preferMaster) {
        return ConnectionPool::instance()->getMasterConnection();
    }
    return ConnectionPool::instance()->getSlaveConnection();
}

std::shared_ptr<MySQL> DatabaseRouter::getConnection(MySQL::DBRole role) {
    return ConnectionPool::instance()->getConnection(role);
}

void DatabaseRouter::returnConnection(std::shared_ptr<MySQL> conn) {
    if (conn) {
        ConnectionPool::instance()->returnConnection(conn);
    }
}