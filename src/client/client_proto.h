#ifndef CLIENT_PROTO_H
#define CLIENT_PROTO_H

#include "../server/proto/message.pb.h"
#include <string>

// 客户端Protobuf消息工具类
class ClientProto {
public:
    // 创建登录请求消息
    static std::string createLoginRequest(int id, const std::string& password);
    
    // 创建注册请求消息
    static std::string createRegisterRequest(const std::string& name, const std::string& password);
    
    // 创建一对一聊天消息
    static std::string createOneChatMessage(int fromid, int toid, const std::string& message, int64_t time);
    
    // 创建添加好友消息
    static std::string createAddFriendRequest(int fromid, int friendid);
    
    // 创建创建群组消息
    static std::string createCreateGroupRequest(int fromid, const std::string& groupname, const std::string& groupdesc);
    
    // 创建加入群组消息
    static std::string createAddGroupRequest(int fromid, int groupid);
    
    // 创建群聊消息
    static std::string createGroupChatMessage(int fromid, int groupid, const std::string& message, int64_t time);
    
    // 创建注销消息
    static std::string createLogoutRequest(int fromid);
};

#endif