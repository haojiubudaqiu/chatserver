#include "groupmodel.hpp"
#include "db.h"

GroupModel::GroupModel() {
    _cacheManager = CacheManager::instance();
}

// 创建群组
bool GroupModel::createGroup(Group &group)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into allgroup(groupname, groupdesc) values('%s', '%s')",
            group.getName().c_str(), group.getDesc().c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        if (mysql.update(sql))
        {
            group.setId(mysql_insert_id(mysql.getConnection()));
            
            // 缓存群组信息
            _cacheManager->cacheGroup(group);
            
            return true;
        }
    }

    return false;
}

// 加入群组
void GroupModel::addGroup(int userid, int groupid, string role)
{
    // 1.组装sql语句
    char sql[1024] = {0};
    sprintf(sql, "insert into groupuser values(%d, %d, '%s')",
            groupid, userid, role.c_str());

    MySQL mysql;
    if (mysql.connect())
    {
        mysql.update(sql);
        
        // 清除群组缓存
        _cacheManager->invalidateGroup(groupid);
    }
}

// 查询用户所在群组信息
vector<Group> GroupModel::queryGroups(int userid)
{
    // 这个方法比较复杂，涉及多个表的联合查询，暂时不缓存整个结果
    // 可以考虑缓存单个群组的信息
    /*
    1. 先根据userid在groupuser表中查询出该用户所属的群组信息
    2. 在根据群组信息，查询属于该群组的所有用户的userid，并且和user表进行多表联合查询，查出用户的详细信息
    */
    char sql[1024] = {0};
    sprintf(sql, "select a.id,a.groupname,a.groupdesc from allgroup a inner join \
         groupuser b on a.id = b.groupid where b.userid=%d",
            userid);

    vector<Group> groupVec;

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            // 查出userid所有的群组信息
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
    }

    // 查询群组的用户信息
    for (Group &group : groupVec)
    {
        sprintf(sql, "select a.id,a.name,a.state,b.grouprole from user a \
            inner join groupuser b on b.userid = a.id where b.groupid=%d",
                group.getId());

        MYSQL_RES *res = mysql.query(sql);
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
    }
    return groupVec;
}

// 根据指定的groupid查询群组用户id列表，除userid自己，主要用户群聊业务给群组其它成员群发消息
vector<int> GroupModel::queryGroupUsers(int userid, int groupid)
{
    char sql[1024] = {0};
    sprintf(sql, "select userid from groupuser where groupid = %d and userid != %d", groupid, userid);

    vector<int> idVec;
    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row;
            while ((row = mysql_fetch_row(res)) != nullptr)
            {
                idVec.push_back(atoi(row[0]));
            }
            mysql_free_result(res);
        }
    }
    return idVec;
}

// 根据群组ID查询群组信息
Group GroupModel::queryGroup(int groupid)
{
    // 先从缓存中查询群组信息
    Group group = _cacheManager->getGroup(groupid);
    if (group.getId() != 0) {
        return group;
    }
    
    // 缓存未命中，从数据库查询
    char sql[1024] = {0};
    sprintf(sql, "select id, groupname, groupdesc from allgroup where id = %d", groupid);

    MySQL mysql;
    if (mysql.connect())
    {
        MYSQL_RES *res = mysql.query(sql);
        if (res != nullptr)
        {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row != nullptr)
            {
                group.setId(atoi(row[0]));
                group.setName(row[1]);
                group.setDesc(row[2]);
                mysql_free_result(res);
                
                // 查询群组成员信息
                sprintf(sql, "select a.id,a.name,a.state,b.grouprole from user a \
                    inner join groupuser b on b.userid = a.id where b.groupid=%d",
                        group.getId());

                MYSQL_RES *res2 = mysql.query(sql);
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
                
                // 将查询结果缓存到Redis
                _cacheManager->cacheGroup(group);
                
                return group;
            }
            mysql_free_result(res);
        }
    }
    return Group();
}