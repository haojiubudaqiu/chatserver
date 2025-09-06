#include "proto_msg_processor.h"
#include <muduo/base/Logging.h>

ProtoMsgProcessorManager* ProtoMsgProcessorManager::instance() {
    static ProtoMsgProcessorManager instance;
    return &instance;
}

void ProtoMsgProcessorManager::processMessage(chat::MsgType msgType, 
                                             const TcpConnectionPtr& conn, 
                                             const std::string& data, 
                                             Timestamp time) {
    auto it = processors_.find(msgType);
    if (it != processors_.end()) {
         // 找到了对应的消息处理器，进入 try 块以捕获处理过程中可能抛出的异常
        try {
             // 通过迭代器 it 获取 second（即 unique_ptr<ProtoMsgProcessor>），
            it->second->process(conn, data, time);
        } catch (const std::exception& e) {
            // 捕获在处理消息过程中抛出的标准异常（及其子类异常）
            LOG_ERROR << "Error processing message type " << msgType << ": " << e.what();
            
            // 发送错误响应
            chat::BaseMessage errorMsg;
            errorMsg.set_msgid(chat::INVALID_MSG);
            errorMsg.set_time(time.microSecondsSinceEpoch());

            // 将 Protobuf 消息对象序列化成二进制字符串并通过 TCP 连接发送
            if (conn && conn->connected()) {
                conn->send(errorMsg.SerializeAsString());
            }
        }
    } else {
        
        // 没有找到注册的处理器，说明收到了一个服务器尚未支持或注册的消息类型
        // 记录一条警告日志，这对于调试和发现协议问题非常有用
        LOG_WARN << "No handler registered for message type: " << msgType;
        
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        
        if (conn && conn->connected()) {
            conn->send(errorMsg.SerializeAsString());
        }
    }
}

bool ProtoMsgProcessorManager::hasHandler(chat::MsgType msgType) const {
    return processors_.find(msgType) != processors_.end();
}