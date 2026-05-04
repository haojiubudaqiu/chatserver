#include "offlinemessagemodel.hpp"
#include "database_router.h"
#include <cstring>

void OfflineMsgModel::insert(int userid, string msg)
{
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn || !conn->getConnection()) return;

    MYSQL* mysql = conn->getConnection();
    std::string escaped(msg.length() * 2 + 1, '\0');
    mysql_real_escape_string(mysql, &escaped[0], msg.c_str(), msg.length());
    
    std::string sql = "insert into offlinemessage values(" 
                    + std::to_string(userid) + ", '" + escaped.c_str() + "')";
    
    conn->update(sql);
    DatabaseRouter::instance()->returnConnection(conn);
}

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

vector<string> OfflineMsgModel::query(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select message from offlinemessage where userid = %d", userid);

    vector<string> vec;
    
    auto conn = DatabaseRouter::instance()->routeQuery();
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
    DatabaseRouter::instance()->returnConnection(conn);
    return vec;
}