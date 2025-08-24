#ifndef PROTO_MSG_HANDLER_H
#define PROTO_MSG_HANDLER_H

#include <functional>
#include <unordered_map>
#include "muduo/net/TcpConnection.h"
#include "muduo/base/Timestamp.h"
#include "message.pb.h"

using namespace muduo;
using namespace muduo::net;

// 消息处理器类型定义
typedef std::function<void(const TcpConnectionPtr&, const std::string&, Timestamp)> ProtoMsgHandler;

class ProtoMsgHandlerMap {
public:
    static ProtoMsgHandlerMap* instance();

    // 获取消息对应的处理器
    ProtoMsgHandler getHandler(chat::MsgType msgType);

    // 注册消息处理器
    void registerHandler(chat::MsgType msgType, const ProtoMsgHandler& handler);

private:
    ProtoMsgHandlerMap() = default;
    std::unordered_map<chat::MsgType, ProtoMsgHandler> _handlerMap;
};

#endif