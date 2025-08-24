#include "chatserver.hpp"
#include "chatservice.hpp"
#include "message.pb.h"
#include "proto_msg_handler.h"

#include <iostream>
//函数对象绑定器
#include <functional>
#include <string>
using namespace std;
using namespace placeholders;//参数占位符

// 初始化聊天服务器对象，构造函数
ChatServer::ChatServer(EventLoop *loop,
                       const InetAddress &listenAddr,
                       const string &nameArg)
    : _server(loop, listenAddr, nameArg), _loop(loop)
{
    // 注册链接回调
    _server.setConnectionCallback(std::bind(&ChatServer::onConnection, this, _1));

    // 注册消息回调
    _server.setMessageCallback(std::bind(&ChatServer::onMessage, this, _1, _2, _3));

    // 设置线程数量
    _server.setThreadNum(4);
}

// 启动服务
void ChatServer::start()
{
    _server.start();
}

// 上报链接相关信息的回调函数
void ChatServer::onConnection(const TcpConnectionPtr &conn)
{
    // 客户端断开链接
    if (!conn->connected())
    {
        ChatService::instance()->clientCloseException(conn);
        conn->shutdown();
    }
}

// 上报读写事件相关信息的回调函数
void ChatServer::onMessage(const TcpConnectionPtr &conn,
                           Buffer *buffer,
                           Timestamp time)
{
    string buf = buffer->retrieveAllAsString();
    
    // 首先尝试解析为Protobuf消息
    chat::BaseMessage baseMsg;
    if (baseMsg.ParseFromString(buf)) {
        // 是Protobuf消息
        auto msgHandler = ProtoMsgHandlerMap::instance()->getHandler(baseMsg.msgid());
        msgHandler(conn, buf, time);
    } else {
        // 如果不是Protobuf消息，可以尝试解析为JSON（向后兼容）
        // 这里为了简化，我们只处理Protobuf消息
        LOG_ERROR << "Failed to parse message as Protobuf";
        
        if (conn->connected()) {
            chat::BaseMessage response;
            response.set_msgid(chat::INVALID_MSG);
            conn->send(response.SerializeAsString());
        }
    }
}