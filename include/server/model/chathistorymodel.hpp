#ifndef CHATHISTORYMODEL_H
#define CHATHISTORYMODEL_H

#include <string>
#include <vector>
using namespace std;

struct ChatRecord {
    int msgType;
    int fromId;
    int toId;
    string content;
    int64_t msgTime;
};

class ChatHistoryModel
{
public:
    bool insert(int msgType, int fromId, int toId, const string& content, int64_t msgTime);
    vector<ChatRecord> queryPrivateChat(int userid1, int userid2, int limit, int64_t beforeTime);
    vector<ChatRecord> queryGroupChat(int groupid, int limit, int64_t beforeTime);
    bool cleanup(int64_t beforeTime);
};

#endif
