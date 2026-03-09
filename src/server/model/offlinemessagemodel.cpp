#include "offlinemessagemodel.hpp"
#include "database_router.h"

// 存储用户的离线消息
void OfflineMsgModel::insert(int userid, string msg)
{
    char sql[1024] = {0};
    sprintf(sql, "insert into offlinemessage values(%d, '%s')", userid, msg.c_str());

    // 离线消息需要持久化到主库，确保不丢失
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (conn) {
        conn->update(sql);
        DatabaseRouter::instance()->returnConnection(conn);
    }
}

// 删除用户的离线消息
void OfflineMsgModel::remove(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "delete from offlinemessage where userid=%d", userid);

    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (conn) {
        conn->update(sql);
        DatabaseRouter::instance()->returnConnection(conn);
    }
}

// 查询用户的离线消息
vector<string> OfflineMsgModel::query(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d", userid);

    vector<string> vec;
    
    // 离线消息读取使用主库，确保读取最新数据
    // 也可以使用从库+forceMaster，但主库更安全
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn) {
        return vec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            vec.push_back(row[0]);
        }
        mysql_free_result(res);
    }
    DatabaseRouter::instance()->returnConnection(conn);
    return vec;
}
