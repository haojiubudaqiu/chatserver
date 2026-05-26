#ifndef OFFLINEMESSAGEMODEL_H
#define OFFLINEMESSAGEMODEL_H

#include <string>
#include <vector>
using namespace std;

class OfflineMsgModel
{
public:
    bool insert(int userid, string msg);
    bool remove(int userid);
    vector<string> query(int userid);
};

#endif