#include "proto_msg_handler.h"

//负责管理消息类型到处理器函数的映射。
ProtoMsgHandlerMap* ProtoMsgHandlerMap::instance() {
    static ProtoMsgHandlerMap instance;
    return &instance;
}


//是一个函数类型（typedef），接收连接指针、消息内容和时间戳，用于处理网络消息。
ProtoMsgHandler ProtoMsgHandlerMap::getHandler(chat::MsgType msgType) {
    auto it = _handlerMap.find(msgType);
    if (it == _handlerMap.end()) {
        // 返回一个默认的处理器lambda（匿名函数），作为“兜底”处理器，可以检查连接有效且已连接

        return [](const TcpConnectionPtr& conn, const std::string& data, Timestamp time) {
            if (conn && conn->connected()) {
                chat::BaseMessage response;
                response.set_msgid(chat::INVALID_MSG);
                // 发送错误响应
                // conn->send(response.SerializeAsString());
            }
        };
    }
    //返回对应处理器
    return it->second;
}

//允许业务层代码为每种消息类型注册自己的处理器（函数或 lambda）
void ProtoMsgHandlerMap::registerHandler(chat::MsgType msgType, const ProtoMsgHandler& handler) {
    _handlerMap[msgType] = handler;
}