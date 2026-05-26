#ifndef FRIENDMODEL_H
#define FRIENDMODEL_H

#include "user.hpp"
#include <vector>
using namespace std;

class FriendModel
{
public:
    FriendModel();
    bool insert(int userid, int friendid);
    vector<User> query(int userid);
};

#endif