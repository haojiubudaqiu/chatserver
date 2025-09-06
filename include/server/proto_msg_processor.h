#ifndef PROTO_MSG_PROCESSOR_H
#define PROTO_MSG_PROCESSOR_H


/*
面向 Protobuf 聊天消息的高级处理框架，
适合用于高性能异步服务器（如 Muduo）
解耦消息分发与具体处理逻辑：业务层只需注册特定类型的消息处理器，框架自动完成解析和分发。
类型安全：每个处理器只处理自己关心的消息类型，避免类型错误。
易扩展：新增消息类型只需注册新处理器，无需改动原有代码。
错误处理一致：解析失败时自动发送错误响应。
*/

#include <functional>
#include <unordered_map>
#include <memory>
#include "muduo/net/TcpConnection.h"
#include "muduo/base/Timestamp.h"
#include "message.pb.h"

using namespace muduo;
using namespace muduo::net;

// Protobuf消息处理器基类，定义了所有 Protobuf 消息处理器的通用接口。
// 这是一个抽象基类，使用多态来实现不同类型的消息处理。
class ProtoMsgProcessor {
public:
    virtual ~ProtoMsgProcessor() = default;
    virtual void process(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) = 0;
};

// 模板化的消息处理器类，允许对每种 Protobuf 消息类型生成专门的处理器。
// MessageType: 模板参数，代表具体的 Protobuf 消息类型（如 chat::LoginRequest）。
template<typename MessageType>
class TypedProtoMsgProcessor : public ProtoMsgProcessor {
public:
    // 定义处理函数的类型。
    typedef std::function<void(const TcpConnectionPtr&, const MessageType&, Timestamp)> HandlerFunction;
    
    // 构造函数，接收一个具体的处理函数并保存起来。
    explicit TypedProtoMsgProcessor(const HandlerFunction& handler) : handler_(handler) {}
    
    // 实现基类的 process 方法，处理收到的消息。
    void process(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) override {
        MessageType message;// 创建一个特定类型的 Protobuf 消息对象
        if (message.ParseFromString(data)) { //尝试将二进制数据解析到消息对象中
            handler_(conn, message, time);  // 解析成功，调用注册的业务处理函数
        } else {
            // 处理解析错误 调用错误处理函数
            handleParseError(conn, data, time);
        }
    }
    
private:
    // 私有方法，处理消息解析失败的情况。
    void handleParseError(const TcpConnectionPtr& conn, const std::string& data, Timestamp time) {
        //创建一个错误响应消息（这里使用 BaseMessage，实际可能需更具体的错误消息）
        chat::BaseMessage errorMsg;
        errorMsg.set_msgid(chat::INVALID_MSG);
        errorMsg.set_time(time.microSecondsSinceEpoch());

         // 检查连接是否有效且仍然连接着
        if (conn && conn->connected()) {
            conn->send(errorMsg.SerializeAsString());
        }
    }
    
    HandlerFunction handler_;
};

// Protobuf消息处理器管理器
//单例类，负责集中管理和分发所有类型的消息处理器。
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