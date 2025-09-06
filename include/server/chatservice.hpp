#ifndef CHATSERVICE_H
#define CHATSERVICE_H

#include <muduo/net/TcpConnection.h>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <thread>
using namespace std;
using namespace muduo;
using namespace muduo::net;

#include "redis.hpp"
#include "groupmodel.hpp"
#include "friendmodel.hpp"
#include "usermodel.hpp"
#include "offlinemessagemodel.hpp"
#include "message.pb.h"
#include "proto_msg_handler.h"
#include "proto_msg_processor.h"
#include "kafka_manager.h"
#include "json.hpp"
using json = nlohmann::json;

// 表示处理消息的事件回调方法类型,用using给已经存在的类型定义一个新的名称
using MsgHandler = std::function<void(const TcpConnectionPtr &conn, json &js, Timestamp)>;

// 聊天服务器业务类
class ChatService
{
public:
    // 获取唯一单例对象的接口函数
    static ChatService *instance();
    // 处理登录业务 (Protobuf版本)
    void login(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 处理注册业务 (Protobuf版本)
    void reg(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 一对一聊天业务 (Protobuf版本)
    void oneChat(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 添加好友业务 (Protobuf版本)
    void addFriend(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 创建群组业务 (Protobuf版本)
    void createGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 加入群组业务 (Protobuf版本)
    void addGroup(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 群组聊天业务 (Protobuf版本)
    void groupChat(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 处理注销业务 (Protobuf版本)
    void loginout(const TcpConnectionPtr &conn, const string &data, Timestamp time);
    // 处理客户端异常退出
    void clientCloseException(const TcpConnectionPtr &conn);
    // 服务器异常，业务重置方法
    void reset();
    // 获取消息对应的处理器
    MsgHandler getHandler(int msgid);
    // 从redis消息队列中获取订阅的消息
    void handleRedisSubscribeMessage(int, string);
    // 从Kafka消息队列中获取订阅的消息
    void handleKafkaMessage(const string& topic, const string& message);

private:
    //因为是单例模式，所以要把构造函数私有化
    ChatService();

    // 存储在线用户的通信连接
    unordered_map<int, TcpConnectionPtr> _userConnMap;
    // 定义互斥锁，保证_userConnMap的线程安全
    mutex _connMutex;

    // 数据操作类对象
    UserModel _userModel;
    OfflineMsgModel _offlineMsgModel;
    FriendModel _friendModel;
    GroupModel _groupModel;

    // redis操作对象
    Redis _redis;
    
    // Kafka管理器
    KafkaManager* _kafkaManager;
    // Kafka消费者线程
    std::thread* _kafkaConsumerThread;
};

#endif