#ifndef PROTO_MSG_HANDLER_H
#define PROTO_MSG_HANDLER_H

/*
用于聊天服务器消息分发和处理的 C++ 头文件。它采用了**“消息类型与处理器函数映射”**的设计，
可以让你的服务端代码非常灵活地应对不同类型的消息。
*/


#include <functional>
#include <unordered_map>
#include "muduo/net/TcpConnection.h"
#include "muduo/base/Timestamp.h"
#include "message.pb.h"

using namespace muduo;
using namespace muduo::net;

// 消息处理器类型定义，本质是一个函数对象（可以是函数、lambda等）
typedef std::function<void(const TcpConnectionPtr&, const std::string&, Timestamp)> ProtoMsgHandler;

class ProtoMsgHandlerMap {
public:
    static ProtoMsgHandlerMap* instance();

    // 获取消息对应的处理器
    ProtoMsgHandler getHandler(chat::MsgType msgType);

    // 注册消息处理器 业务层可以注册不同的消息处理器（比如登录、注册、聊天等），让分发逻辑灵活扩展、解耦。
    void registerHandler(chat::MsgType msgType, const ProtoMsgHandler& handler);

private:
    ProtoMsgHandlerMap() = default;
    std::unordered_map<chat::MsgType, ProtoMsgHandler> _handlerMap;//核心成员，保存所有 msgType 到处理器的映射关系。

};

#endif