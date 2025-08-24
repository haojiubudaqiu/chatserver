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
        try {
            it->second->process(conn, data, time);
        } catch (const std::exception& e) {
            LOG_ERROR << "Error processing message type " << msgType << ": " << e.what();
            
            // 发送错误响应
            chat::BaseMessage errorMsg;
            errorMsg.set_msgid(chat::INVALID_MSG);
            errorMsg.set_time(time.microSecondsSinceEpoch());
            
            if (conn && conn->connected()) {
                conn->send(errorMsg.SerializeAsString());
            }
        }
    } else {
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