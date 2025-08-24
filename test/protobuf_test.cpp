#include <iostream>
#include <string>
#include "message.pb.h"

int main() {
    // 创建一个登录请求消息
    chat::LoginRequest loginReq;
    loginReq.mutable_base()->set_msgid(chat::LOGIN_MSG);
    loginReq.mutable_base()->set_fromid(1001);
    loginReq.set_id(1001);
    loginReq.set_password("123456");
    
    // 序列化消息
    std::string serializedData;
    if (loginReq.SerializeToString(&serializedData)) {
        std::cout << "Serialized login request, size: " << serializedData.size() << " bytes" << std::endl;
        
        // 反序列化消息
        chat::LoginRequest parsedReq;
        if (parsedReq.ParseFromString(serializedData)) {
            std::cout << "Parsed login request:" << std::endl;
            std::cout << "  MsgId: " << parsedReq.base().msgid() << std::endl;
            std::cout << "  FromId: " << parsedReq.base().fromid() << std::endl;
            std::cout << "  UserId: " << parsedReq.id() << std::endl;
            std::cout << "  Password: " << parsedReq.password() << std::endl;
        } else {
            std::cerr << "Failed to parse login request" << std::endl;
        }
    } else {
        std::cerr << "Failed to serialize login request" << std::endl;
    }
    
    return 0;
}