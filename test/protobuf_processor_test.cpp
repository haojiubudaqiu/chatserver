#include "proto_msg_processor.h"
#include "message.pb.h"
#include <iostream>
#include <muduo/base/Timestamp.h>
#include <muduo/net/TcpConnection.h>

using namespace muduo;
using namespace muduo::net;

// 模拟TCP连接处理器
void handleLoginMessage(const TcpConnectionPtr& conn, const chat::LoginRequest& request, Timestamp time) {
    std::cout << "Processing login request:" << std::endl;
    std::cout << "  UserID: " << request.id() << std::endl;
    std::cout << "  Password: " << request.password() << std::endl;
    
    // 创建响应消息
    chat::LoginResponse response;
    response.mutable_base()->set_msgid(chat::LOGIN_MSG_ACK);
    response.mutable_base()->set_time(time.microSecondsSinceEpoch());
    response.set_errno(0);
    response.set_errmsg("Login successful");
    
    // 设置用户信息
    chat::User* user = response.mutable_user();
    user->set_id(request.id());
    user->set_name("testuser");
    user->set_state("online");
    
    std::cout << "Login response created successfully" << std::endl;
}

int main() {
    std::cout << "Testing Protobuf Message Processor..." << std::endl;
    
    // 获取处理器管理器实例
    ProtoMsgProcessorManager* manager = ProtoMsgProcessorManager::instance();
    
    // 注册登录消息处理器
    manager->registerHandler<chat::LoginRequest>(chat::LOGIN_MSG, handleLoginMessage);
    
    // 创建测试登录请求
    chat::LoginRequest request;
    request.mutable_base()->set_msgid(chat::LOGIN_MSG);
    request.mutable_base()->set_time(Timestamp::now().microSecondsSinceEpoch());
    request.set_id(1001);
    request.set_password("testpass123");
    
    // 序列化消息
    std::string serializedData;
    if (request.SerializeToString(&serializedData)) {
        std::cout << "Serialized login request, size: " << serializedData.size() << " bytes" << std::endl;
        
        // 处理消息
        manager->processMessage(chat::LOGIN_MSG, nullptr, serializedData, Timestamp::now());
        
        std::cout << "Message processed successfully" << std::endl;
    } else {
        std::cout << "Failed to serialize login request" << std::endl;
        return -1;
    }
    
    return 0;
}