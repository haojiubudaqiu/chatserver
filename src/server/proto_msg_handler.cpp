#include "proto_msg_handler.h"

ProtoMsgHandlerMap* ProtoMsgHandlerMap::instance() {
    static ProtoMsgHandlerMap instance;
    return &instance;
}

ProtoMsgHandler ProtoMsgHandlerMap::getHandler(chat::MsgType msgType) {
    auto it = _handlerMap.find(msgType);
    if (it == _handlerMap.end()) {
        // 返回一个默认的处理器
        return [](const TcpConnectionPtr& conn, const std::string& data, Timestamp time) {
            if (conn && conn->connected()) {
                chat::BaseMessage response;
                response.set_msgid(chat::INVALID_MSG);
                // 发送错误响应
                // conn->send(response.SerializeAsString());
            }
        };
    }
    return it->second;
}

void ProtoMsgHandlerMap::registerHandler(chat::MsgType msgType, const ProtoMsgHandler& handler) {
    _handlerMap[msgType] = handler;
}