#ifndef GROUPMODEL_H
#define GROUPMODEL_H

#include "group.hpp"
#include "cache_manager.h"
#include <string>
#include <vector>
using namespace std;

class GroupModel
{
public:
    GroupModel();
    bool createGroup(Group &group);
    bool addGroup(int userid, int groupid, string role);
    vector<Group> queryGroups(int userid);
    vector<int> queryGroupUsers(int userid, int groupid);
    Group queryGroup(int groupid, bool forceMaster = false);
    Group queryGroupByName(const string& groupname, bool forceMaster = false);
};

#endif