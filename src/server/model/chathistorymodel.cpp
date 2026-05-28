#include "chathistorymodel.hpp"
#include "connection_guard.h"
#include "database_router.h"
#include <cstring>
#include <muduo/base/Logging.h>

bool ChatHistoryModel::insert(int msgType, int fromId, int toId, const string& content, int64_t msgTime)
{
    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn || !conn->getConnection()) return false;

    MYSQL* mysql = conn->getConnection();
    std::string escaped(content.length() * 2 + 1, '\0');
    size_t escapedLen = mysql_real_escape_string(mysql, &escaped[0], content.c_str(), content.length());

    char sql[8192] = {0};
    int n = snprintf(sql, sizeof(sql),
        "insert into chat_message(msg_type, from_id, to_id, content, msg_time) "
        "values(%d, %d, %d, '%.*s', %lld)",
        msgType, fromId, toId, (int)escapedLen, escaped.c_str(), (long long)msgTime);
    if (n < 0 || (size_t)n >= sizeof(sql)) {
        LOG_ERROR << "SQL too long for chat_message insert (" << n << " bytes needed)";
        return false;
    }

    return conn->update(sql);
}

vector<ChatRecord> ChatHistoryModel::queryPrivateChat(int userid1, int userid2, int limit, int64_t beforeTime)
{
    vector<ChatRecord> vec;

    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery(true));
    if (!conn) return vec;

    char sql[1024] = {0};
    if (beforeTime > 0) {
        snprintf(sql, sizeof(sql),
            "select msg_type, from_id, to_id, content, msg_time from chat_message "
            "where msg_type=1 and ((from_id=%d and to_id=%d) or (from_id=%d and to_id=%d)) "
            "and msg_time < %lld "
            "order by msg_time desc limit %d",
            userid1, userid2, userid2, userid1,
            (long long)beforeTime, limit);
    } else {
        snprintf(sql, sizeof(sql),
            "select msg_type, from_id, to_id, content, msg_time from chat_message "
            "where msg_type=1 and ((from_id=%d and to_id=%d) or (from_id=%d and to_id=%d)) "
            "order by msg_time desc limit %d",
            userid1, userid2, userid2, userid1, limit);
    }

    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            if (row[0] != nullptr) {
                ChatRecord rec;
                rec.msgType = atoi(row[0]);
                rec.fromId = atoi(row[1]);
                rec.toId = atoi(row[2]);
                rec.content = row[3] ? row[3] : "";
                rec.msgTime = row[4] ? atoll(row[4]) : 0;
                vec.push_back(rec);
            }
        }
        mysql_free_result(res);
    }
    return vec;
}

vector<ChatRecord> ChatHistoryModel::queryGroupChat(int groupid, int limit, int64_t beforeTime)
{
    vector<ChatRecord> vec;

    ConnectionGuard conn(DatabaseRouter::instance()->routeQuery(true));
    if (!conn) return vec;

    char sql[1024] = {0};
    if (beforeTime > 0) {
        snprintf(sql, sizeof(sql),
            "select msg_type, from_id, to_id, content, msg_time from chat_message "
            "where msg_type=2 and to_id=%d and msg_time < %lld "
            "order by msg_time desc limit %d",
            groupid, (long long)beforeTime, limit);
    } else {
        snprintf(sql, sizeof(sql),
            "select msg_type, from_id, to_id, content, msg_time from chat_message "
            "where msg_type=2 and to_id=%d "
            "order by msg_time desc limit %d",
            groupid, limit);
    }

    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            if (row[0] != nullptr) {
                ChatRecord rec;
                rec.msgType = atoi(row[0]);
                rec.fromId = atoi(row[1]);
                rec.toId = atoi(row[2]);
                rec.content = row[3] ? row[3] : "";
                rec.msgTime = row[4] ? atoll(row[4]) : 0;
                vec.push_back(rec);
            }
        }
        mysql_free_result(res);
    }
    return vec;
}

bool ChatHistoryModel::cleanup(int64_t beforeTime)
{
    ConnectionGuard conn(DatabaseRouter::instance()->routeUpdate());
    if (!conn) return false;

    char sql[256] = {0};
    snprintf(sql, sizeof(sql), "delete from chat_message where msg_time < %lld", (long long)beforeTime);
    
    bool ret = conn->update(sql);
    if (ret) {
        LOG_INFO << "Cleaned up chat_message records older than " << beforeTime;
    }
    return ret;
}