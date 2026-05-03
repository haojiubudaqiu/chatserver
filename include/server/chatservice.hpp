#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <mutex>
using namespace std;
using namespace muduo;
using namespace muduo::net;

#include "groupmodel.hpp"
#include "friendmodel.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "message.pb.h"
#include "proto_msg_handler.h"
#include "kafka_manager.h"

class ChatService
{
public:
    static ChatService *instance();

    void login(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void reg(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void oneChat(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void addFriend(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void createGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void addGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void groupChat(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void loginout(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    void clientCloseException(const TcpConnectionPtr &conn);
    void reset();
    void handleKafkaMessage(const string& topic, const string& message);

private:
    ChatService();

    unordered_map<int, TcpConnectionPtr> _userConnMap;
    mutex _connMutex;

    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    KafkaManager* _kafkaManager;
};

#endif