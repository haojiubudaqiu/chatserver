#ifndef DATABASE_ROUTER_H
#define DATABASE_ROUTER_H

#include "db.h"
#include "connection_pool.h"
#include <memory>
#include <mutex>

class DatabaseRouter {
public:

    static DatabaseRouter* instance();
    std::shared_ptr<MySQL> routeUpdate();
    std::shared_ptr<MySQL> routeQuery(bool preferMaster = false);
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    void returnConnection(std::shared_ptr<MySQL> conn);

private:
    DatabaseRouter();
    ~DatabaseRouter();
    static DatabaseRouter* instance_;
    static std::mutex mutex_;
};

#endif