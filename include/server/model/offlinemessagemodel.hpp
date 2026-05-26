#ifndef OFFLINEMESSAGEMODEL_H
#define OFFLINEMESSAGEMODEL_H

#include <string>
#include <vector>
using namespace std;

// 提供离线消息表的操作接口方法
class OfflineMsgModel
{
public:
    // 存储用户的离线消息，返回是否成功
    bool insert(int userid, string msg);

    // 删除用户的离线消息，返回是否成功
    bool remove(int userid);

    // 查询用户的离线消息
    vector<string> query(int userid);
};

#endif