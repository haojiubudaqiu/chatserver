#ifndef DATABASE_ROUTER_H
#define DATABASE_ROUTER_H

#include "db.h"
#include "connection_pool.h"
#include <memory>
#include <mutex>

/**
 * 这个 DatabaseRouter 类是一个更高层次的抽象，
 * 它在连接池之上提供了一个智能路由层，实现了自动的读写分离
 * 实现自动读写分离：
 * 自动将写操作（UPDATE, INSERT, DELETE）路由到主库，
 * 将读操作（SELECT）路由到从库。
 * 提供灵活性：允许在特定情况下覆盖自动路由策略（例如，强制读操作使用主库）
 * 统一接口：为应用程序提供一套简单一致的API来获取数据库连接。
 */





// 数据库路由类，用于自动路由读写操作到主库或从库
class DatabaseRouter {
public:

    // 获取路由器单例实例的静态方法
    static DatabaseRouter* instance();
    
    // 路由更新操作到主库
    // 核心路由方法1：路由更新操作到主库
    // 任何写操作（INSERT, UPDATE, DELETE）都应使用此方法
    std::shared_ptr<MySQL> routeUpdate();
    
    // 路由查询操作到从库（可选择使用主库）
    // 核心路由方法2：路由查询操作到从库（可选择使用主库）
    // 参数preferMaster: 如果为true，则强制使用主库进行查询
    // 使用场景：需要读取刚刚写入的数据（读写一致性），或者从库延迟较高时
    std::shared_ptr<MySQL> routeQuery(bool preferMaster = false);
    
    // 手动路由到指定角色的数据库
    // 提供了完全的手动控制，绕过自动路由策略
    std::shared_ptr<MySQL> getConnection(MySQL::DBRole role);
    
    // 归还连接
    void returnConnection(std::shared_ptr<MySQL> conn);

private:
    DatabaseRouter();
    ~DatabaseRouter();
    // 静态单例实例指针
    static DatabaseRouter* instance_;
    // 用于保护单例创建过程的互斥锁（线程安全）
    static std::mutex mutex_;
};

#endif