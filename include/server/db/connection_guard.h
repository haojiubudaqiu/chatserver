
#ifndef CONNECTION_GUARD_H
#define CONNECTION_GUARD_H

#include <memory>
#include "mysql.h"
#include "database_router.h"

class ConnectionGuard {
public:
    ConnectionGuard(std::shared_ptr<MySQL> conn) : conn_(conn) {}
    ~ConnectionGuard() {
        if (conn_) {
            DatabaseRouter::instance()->returnConnection(conn_);
        }
    }

    std::shared_ptr<MySQL> get() { return conn_; }
    MySQL* operator->() { return conn_.get(); }
    operator bool() const { return conn_ != nullptr; }

private:
    std::shared_ptr<MySQL> conn_;
    // Non-copyable
    ConnectionGuard(const ConnectionGuard&) = delete;
    ConnectionGuard& operator=(const ConnectionGuard&) = delete;
};

#endif
