#include "connection_guard.h"
#include "offlinemessagemodel.hpp"
#include "database_router.h"
#include <cstring>

bool OfflineMsgModel::insert(int userid, string msg)
{
    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn || !conn->getConnection()) return false;

    MYSQL* mysql = conn->getConnection();
    std::string escaped(msg.length() * 2 + 1, '\0');
    mysql_real_escape_string(mysql, &escaped[0], msg.c_str(), msg.length());
    
    std::string sql = "insert into offlinemessage(userid, message) values(" 
                    + std::to_string(userid) + ", '" + escaped.c_str() + "')";
    
    return conn->update(sql);
}

bool OfflineMsgModel::remove(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "delete from offlinemessage where userid=%d", userid);

    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn) {
        return false;
    }
    return conn->update(sql);
}

vector<string> OfflineMsgModel::query(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d", userid);

    vector<string> vec;
    
    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery(true));
    if (!conn) {
        return vec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while((row = mysql_fetch_row(res)) != nullptr)
        {
            if (row[0] != nullptr) {
                vec.push_back(row[0]);
            }
        }
        mysql_free_result(res);
    }
    return vec;
}