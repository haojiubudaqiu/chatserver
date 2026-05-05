#include "groupmodel.hpp"
#include "database_router.h"
#include "cache_manager.h"

GroupModel::GroupModel() {
    _cacheManager = CacheManager::instance();
}

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn) {
        return false;
    }

    char name_escaped[256];
    char desc_escaped[256];
    mysql_real_escape_string(conn->getConnection(), name_escaped, group.getName().c_str(), group.getName().length());
    mysql_real_escape_string(conn->getConnection(), desc_escaped, group.getDesc().c_str(), group.getDesc().length());

    char sql[1024] = {0};
    sprintf(sql, "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
            name_escaped, desc_escaped);

    if (conn->update(sql))
    {
        group.setId(mysql_insert_id(conn->getConnection()));
        
        // 缓存群组信息
        _cacheManager->cacheGroup(group);
        
        DatabaseRouter::instance()->returnConnection(conn);
        return true;
    }
    
    DatabaseRouter::instance()->returnConnection(conn);
    return false;
}

// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    auto conn = DatabaseRouter::instance()->routeUpdate();
    if (!conn) {
        return;
    }

    char role_escaped[256];
    mysql_real_escape_string(conn->getConnection(), role_escaped, role.c_str(), role.length());

    char sql[1024] = {0};
    sprintf(sql, "insert into groupuser values(%d, %d, '%s')",
            groupid, userid, role_escaped);

    conn->update(sql);
    DatabaseRouter::instance()->returnConnection(conn);
    
    // 清除群组缓存
    _cacheManager->invalidateGroup(groupid);
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.groupname,a.groupdesc from allgroup a inner join \
         groupuser b on a.id = b.groupid where b.userid=%d",
            userid);

    vector<Group> groupVec;

    // 读操作，使用从库
    auto conn = DatabaseRouter::instance()->routeQuery();
    if (!conn) {
        return groupVec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            Group group;
            group.setId(atoi(row[0]));
            group.setName(row[1]);
            group.setDesc(row[2]);
            groupVec.push_back(group);
        }
        mysql_free_result(res);
    }
    DatabaseRouter::instance()->returnConnection(conn);

    // 查询群组的用户信息
    for (Group &group : groupVec)
    {
        sprintf(sql, "select a.id,a.name,a.state,b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid=%d",
                group.getId());

        conn = DatabaseRouter::instance()->routeQuery();
        if (!conn) continue;
        
        MYSQL_RES *res = conn->query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                GroupUser user;
                user.setId(atoi(row[0]));
                user.setName(row[1]);
                user.setState(row[2]);
                user.setRole(row[3]);
                group.getUsers().push_back(user);
            }
            mysql_free_result(res);
        }
        DatabaseRouter::instance()->returnConnection(conn);
    }
    return groupVec;
}

// 根据指定的groupid查询群组用户id列表，除userid自己
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    auto conn = DatabaseRouter::instance()->routeQuery();
    if (!conn) {
        return idVec;
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row;
        while ((row = mysql_fetch_row(res)) != nullptr)
        {
            idVec.push_back(atoi(row[0]));
        }
        mysql_free_result(res);
    }
    DatabaseRouter::instance()->returnConnection(conn);
    return idVec;
}

// 根据群组ID查询群组信息
Group GroupModel::queryGroup(int groupid)
{
    // 1. 先从Redis缓存查询（群组是高频访问数据）
    Group group = _cacheManager->getGroup(groupid);
    if (group.getId() != 0) {
        return group;
    }
    
    // 2. 缓存未命中，从数据库查询
    char sql[1024] = {0};
    sprintf(sql, "select id, groupname, groupdesc from allgroup where id = %d", groupid);

    auto conn = DatabaseRouter::instance()->routeQuery();
    if (!conn) {
        return Group();
    }
    
    MYSQL_RES *res = conn->query(sql);
    if (res != nullptr)
    {
        MYSQL_ROW row = mysql_fetch_row(res);
        if (row != nullptr)
        {
            group.setId(atoi(row[0]));
            group.setName(row[1]);
            group.setDesc(row[2]);
            mysql_free_result(res);
            
            sprintf(sql, "select a.id,a.name,a.state,b.grouprole from user a \
                inner join groupuser b on b.userid = a.id where b.groupid=%d",
                    group.getId());

            MYSQL_RES *res2 = conn->query(sql);
            if (res2 != nullptr)
            {
                MYSQL_ROW row2;
                while ((row2 = mysql_fetch_row(res2)) != nullptr)
                {
                    GroupUser user;
                    user.setId(atoi(row2[0]));
                    user.setName(row2[1]);
                    user.setState(row2[2]);
                    user.setRole(row2[3]);
                    group.getUsers().push_back(user);
                }
                mysql_free_result(res2);
            }
            
        _cacheManager->cacheGroup(group);
    }
}
    DatabaseRouter::instance()->returnConnection(conn);
    return group;
}
