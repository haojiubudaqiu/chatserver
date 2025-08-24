#ifndef PROTO_MSG_PROCESSOR_H
#define PROTO_MSG_PROCESSOR_H

#include <functional>
#include <unordered_map>
#include <memory>
#include "muduo/net/TcpConnection.h"
#include "muduo/base/Timestamp.h"
#include "message.pb.h"

using namespace muduo;
using namespace muduo::net;

// Protobuf消息处理器基类
class ProtoMsgProcessor {
public:
    virtual ~ProtoMsgProcessor() = default;
    virtual void process(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) = 0;
};

// 模板化的消息处理器类
template<typename MessageType>
class TypedProtoMsgProcessor : public ProtoMsgProcessor {
public:
    typedef std::function<void(const TcpConnectionPtr&, const MessageType&, Timestamp)> HandlerFunction;
    
    explicit TypedProtoMsgProcessor(const HandlerFunction& handler) : handler_(handler) {}
    
    void process(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) override {
        MessageType message;
        if (message.ParseFromString(data)) {
            handler_(conn, message, time);
        } else {
            // 处理解析错误
            handleParseError(conn, data, time);
        }
    }
    
private:
    void handleParseError(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) {
        // 发送错误响应
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());
        
        if (conn && conn->connected()) {
            conn->send(errorMsg.SerializeAsString());
        }
    }
    
    HandlerFunction handler_;
};

// Protobuf消息处理器管理器
class ProtoMsgProcessorManager {
public:
    static ProtoMsgProcessorManager* instance();
    
    // 注册消息处理器
    template<typename MessageType>
    void registerHandler(chat::MsgType msgType, 
                        typename TypedProtoMsgProcessor<MessageType>::HandlerFunction handler) {
        processors_[msgType] = std::make_unique<TypedProtoMsgProcessor<MessageType>>(handler);
    }
    
    // 处理消息
    void processMessage(chat::MsgType msgType, const TcpConnectionPtr& conn, 
                       const std::string& data, Timestamp time);
    
    // 检查是否存在处理器
    bool hasHandler(chat::MsgType msgType) const;
    
private:
    ProtoMsgProcessorManager() = default;
    std::unordered_map<chat::MsgType, std::unique_ptr<ProtoMsgProcessor>> processors_;
};

#endif